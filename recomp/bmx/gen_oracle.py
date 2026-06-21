#!/usr/bin/env python3
"""gen_oracle.py <dynsyms_file> <out_dir> <verfile>
Generalised oracle stub generator for libs with versioned deps from MULTIPLE
sonames — including the case where the SAME version (e.g. Qt_5) is required from
several files. Each VERNEED entry (file F, version V) needs F to be loaded AND to
define version V, so we emit one stub per file, and every stub defines each
version it is listed for (with that version's symbols — duplicated across files,
which is fine for shared objects). Plus a general gen_stub.c for the unversioned
non-glibc imports. Inputs: a `readelf --dyn-syms -W` dump and a version->soname
map (<verfile>, possibly many 'V F' lines per V).  Writes oracle_manifest.txt.
"""
import sys
dynpath, out, verpath = sys.argv[1], sys.argv[2], sys.argv[3]
GLIBC = ("GLIBC", "CXXABI", "GCC_")

# file -> set(versions it must define)  (a version may appear under several files)
file2vers = {}
for line in open(verpath):
    line = line.strip()
    if not line:
        continue
    v, f = line.split()
    file2vers.setdefault(f, set()).add(v)

dyn = open(dynpath).read()
plain = []
ver2syms = {}
for l in dyn.splitlines():
    p = l.split()
    if len(p) >= 8 and p[6] == "UND" and p[4] in ("GLOBAL", "WEAK") and p[3] != "NOTYPE":
        nm = p[7]
        if "@" in nm:
            base, ver = nm.split("@", 1)
            if any(s in ver for s in GLIBC):
                continue
            ver2syms.setdefault(ver, set()).add(base)
        else:
            if not nm.startswith(("_ITM", "__gmon")):
                plain.append(nm)

manifest = []
gc = ["/* general unversioned-import stub */"]
for i, nm in enumerate(sorted(set(plain))):
    gc.append(f'long _g{i} __asm__("{nm}") = 0;')
open(f"{out}/gen_stub.c", "w").write("\n".join(gc) + "\n")
manifest.append("GEN gen_stub.c")

for fi, (soname, versions) in enumerate(sorted(file2vers.items())):
    safe = soname.replace(".", "_")
    c = [f"/* stub carrying soname {soname} */"]
    vs = []
    si = 0
    for ver in sorted(versions):
        syms = sorted(ver2syms.get(ver, set()))
        # unique C symbol names per stub (the asm name is the real exported symbol)
        for s in syms:
            c.append(f'long _f{fi}_{si} __asm__("{s}") = 0;')
            si += 1
        if syms:
            vs.append(ver + " {\n  global:\n" + "".join(f"    {s};\n" for s in syms) + "};\n")
        else:
            vs.append(ver + " {\n};\n")
    open(f"{out}/stub_{safe}.c", "w").write("\n".join(c) + "\n")
    open(f"{out}/stub_{safe}.ver", "w").write("".join(vs))
    manifest.append(f"STUB {soname} stub_{safe}.c stub_{safe}.ver")

open(f"{out}/oracle_manifest.txt", "w").write("\n".join(manifest) + "\n")
print("\n".join(manifest))
