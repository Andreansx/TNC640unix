import sys, re, idapro, idaapi, idautils, ida_funcs, ida_name, ida_hexrays, idc
PATS = [
 ('flatB',  re.compile(r'^return \*\(\(unsigned __int8 \*\)this \+ (\d+)\);$')),
 ('flatDk', re.compile(r'^return \*\(\(_DWORD \*\)this \+ (\d+)\);$')),
 ('pimplDk',re.compile(r'^return \*\(_DWORD \*\)\(\*\(\(_DWORD \*\)this \+ (\d+)\) \+ (\d+)\);$')),
 ('pimplB', re.compile(r'^return \*\(unsigned __int8 \*\)\(\*\(\(_DWORD \*\)this \+ (\d+)\) \+ (\d+)\);$')),
]
for so in sys.argv[1:]:
    base=so.split("\\")[-1].replace(".so","")
    try: idapro.open_database(so, True)
    except Exception as e: print("OPENFAIL\t%s\t%s"%(base,e)); continue
    try: ida_hexrays.init_hexrays_plugin()
    except: pass
    seen=set()
    for ea in idautils.Functions():
        name=ida_funcs.get_func_name(ea) or ""
        if name.startswith(('.','__x86')) or '@' in name or name in seen: continue
        f=ida_funcs.get_func(ea)
        if not f or (f.flags & ida_funcs.FUNC_THUNK): continue
        if (f.end_ea-f.start_ea) > 32: continue
        try:
            if not ida_name.is_public_name(ea): continue
        except: pass
        try: txt=str(ida_hexrays.decompile(ea))
        except: continue
        lines=[l.strip() for l in txt.splitlines()]
        stmts=[l for l in lines if l.endswith(';') and not l.startswith('//')]
        rets=[l for l in stmts if l.startswith('return ')]
        if len(stmts)!=1 or len(rets)!=1: continue
        if any(k in txt for k in ('if (','while(','for (','for(','? ')): continue
        for kind,pat in PATS:
            m=pat.match(rets[0])
            if not m: continue
            seen.add(name)
            if kind=='flatB':    print("%s\t%s\tflatB\t%d"%(base,name,int(m.group(1))))
            elif kind=='flatDk': print("%s\t%s\tflatD\t%d"%(base,name,int(m.group(1))*4))
            elif kind=='pimplDk':print("%s\t%s\tpimplD\t%d\t%d"%(base,name,int(m.group(1))*4,int(m.group(2))))
            elif kind=='pimplB': print("%s\t%s\tpimplB\t%d\t%d"%(base,name,int(m.group(1))*4,int(m.group(2))))
            break
    idapro.close_database(False)
