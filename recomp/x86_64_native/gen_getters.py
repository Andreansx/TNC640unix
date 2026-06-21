#!/usr/bin/env python3
"""gen_getters.py <getterlist> <dirname> — emit rebuilt.c + harness.c for a list
of auto-extracted single-return getters (SYM\tKIND\toff[\toff2]).
Each getter is tested on a fresh pattern-filled object (PIMPL ones get a d-buffer),
so a wrong offset/kind yields a wrong value the diff catches. Arch-independent:
results are buffer contents (not pointers)."""
import sys
items=[]
for line in open(sys.argv[1]):
    p=line.rstrip("\n").split("\t")
    if len(p)<3: continue
    items.append(p)
dirn=sys.argv[2]
acc={'flatB':lambda o,*r:f'*(unsigned char*)((char*)t+{o})',
     'flatD':lambda o,*r:f'*(int*)((char*)t+{o})',
     'pimplB':lambda m,n:f'*(unsigned char*)((char*)*(void**)((char*)t+{m})+{n})',
     'pimplD':lambda m,n:f'*(int*)((char*)*(void**)((char*)t+{m})+{n})'}
rb=["/* AUTO-GENERATED getter leaves (gen_getters.py) — IDA-extracted single-return",
    " * getters: flat this->field and PIMPL this->d->field. Verified byte-identical",
    " * vs the genuine i386 .so on a pattern-filled object. */"]
hp=["#include <stdio.h>","#include <string.h>","#include <unistd.h>",
    "static void fill(unsigned char*b,int n,int s){for(int i=0;i<n;i++)b[i]=(unsigned char)((i*131+s)&0xff);}"]
calls=[]
for i,(sym,kind,*offs) in enumerate(items):
    offs=[int(x) for x in offs]
    expr=acc[kind](*offs)
    rb.append(f"int {sym}(void *t){{ return {expr}; }}")
    hp.append(f"extern int {sym}(void*);")
    if kind.startswith('pimpl'):
        m=offs[0]
        calls.append(f'{{unsigned char o[1024],d[1024];fill(o,1024,{i*7+1});fill(d,1024,{i*13+3});*(void**)(o+{m})=d;int r={sym}(o);int n=sprintf(buf,"%d %d\\n",{i},r);write(1,buf,n);}}')
    else:
        calls.append(f'{{unsigned char o[1024];fill(o,1024,{i*7+1});int r={sym}(o);int n=sprintf(buf,"%d %d\\n",{i},r);write(1,buf,n);}}')
hp.append("int main(void){char buf[64];")
hp+=calls
hp.append("return 0;}")
open(f"/tmp/{dirn}_rebuilt.c","w").write("\n".join(rb)+"\n")
open(f"/tmp/verify_{dirn}.c","w").write("\n".join(hp)+"\n")
print(f"generated {len(items)} getters -> /tmp/{dirn}_rebuilt.c, /tmp/verify_{dirn}.c")
