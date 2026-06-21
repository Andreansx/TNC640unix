#!/usr/bin/env python3
"""compare_metaval.py truth.txt recomp.txt — behavioral comparator.
Integer results exact; double results within <=2 ULP (or abs<=1e-12)."""
import sys, struct

def ulp(a, b):
    ua = struct.unpack("<q", struct.pack("<d", a))[0]
    ub = struct.unpack("<q", struct.pack("<d", b))[0]
    if ua < 0: ua = (1 << 63) - ua
    if ub < 0: ub = (1 << 63) - ub
    return abs(ua - ub)

def load(p):
    I, D = {}, {}
    for ln in open(p):
        f = ln.split()
        if not f: continue
        if f[0] == "I": I[int(f[1])] = int(f[2])
        elif f[0] == "D": D[int(f[1])] = (int(f[2], 16), float(f[3]))
    return I, D

tI, tD = load(sys.argv[1])
rI, rD = load(sys.argv[2])
fails = []
if tI.keys() != rI.keys(): fails.append("int index set differs")
for i in tI:
    if tI[i] != rI.get(i):
        fails.append(f"I[{i}] {tI[i]} != {rI.get(i)}")
maxu = 0
if tD.keys() != rD.keys(): fails.append("double index set differs")
for i in tD:
    tb, tv = tD[i]; rb, rv = rD[i]
    u = ulp(tv, rv); maxu = max(maxu, u)
    if u > 2 and abs(tv - rv) > 1e-12:
        fails.append(f"D[{i}] {tv!r} != {rv!r} ({u} ULP)")
print(f"int results: {len(tI)} (exact)   double results: {len(tD)}  max {maxu} ULP")
if fails:
    print("RESULT: MISMATCH")
    for f in fails[:40]: print("  " + f)
    sys.exit(1)
print("RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386")
print(f"  (all {len(tI)} integer/boolean results exact; all {len(tD)} doubles within {maxu} ULP)")
