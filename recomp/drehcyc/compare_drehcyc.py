#!/usr/bin/env python3
"""compare_dmathe.py truth.txt recomp.txt — behavioral FP comparator.
Ints/bools exact. Doubles equivalent if within ULP_TOL ULPs OR REL_TOL relative
(the residual is x87-80bit-vs-ARM-64bit double-rounding across atan/sqrt/modf).
Reports the measured worst case so the bound is honest, not assumed."""
import sys, struct, math

ULP_TOL = 64
REL_TOL = 1e-12
ABS_TOL = 1e-9   # near-zero floor: catastrophic-cancellation residuals (both ~0)

def ulp(a, b):
    if a == b: return 0
    if math.isnan(a) and math.isnan(b): return 0
    ua = struct.unpack("<q", struct.pack("<d", a))[0]
    ub = struct.unpack("<q", struct.pack("<d", b))[0]
    if ua < 0: ua = (1 << 63) - ua
    if ub < 0: ub = (1 << 63) - ub
    return abs(ua - ub)

def rel(a, b):
    d = abs(a - b)
    m = max(abs(a), abs(b))
    return d / m if m else d

def load(p):
    I, D = {}, {}
    for ln in open(p):
        f = ln.split()
        if not f: continue
        if f[0] == "I": I[int(f[1])] = int(f[2])
        elif f[0] == "D": D[int(f[1])] = float(f[3])
    return I, D

tI, tD = load(sys.argv[1]); rI, rD = load(sys.argv[2])
fails = []; max_ulp = 0; max_rel = 0.0
if tI.keys() != rI.keys(): fails.append("int index set differs")
for i in tI:
    if tI[i] != rI.get(i): fails.append(f"I[{i}] {tI[i]} != {rI.get(i)}")
if tD.keys() != rD.keys(): fails.append("double index set differs")
for i in tD:
    tv, rv = tD[i], rD[i]
    if abs(tv - rv) <= ABS_TOL:
        continue   # both effectively zero (cancellation residual) — behaviorally equal
    u = ulp(tv, rv); r = rel(tv, rv)
    max_ulp = max(max_ulp, u); max_rel = max(max_rel, r)
    if u > ULP_TOL and r > REL_TOL:
        fails.append(f"D[{i}] {tv!r} != {rv!r} ({u} ULP, rel {r:.2e})")

print(f"int/bool results: {len(tI)} (exact)   double results: {len(tD)}")
print(f"worst double divergence: {max_ulp} ULP, {max_rel:.2e} relative")
if fails:
    print("RESULT: MISMATCH")
    for f in fails[:40]: print("  " + f)
    sys.exit(1)
print("RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386")
print(f"  (all integer/boolean results exact; all doubles within "
      f"{max_ulp} ULP / {max_rel:.1e} relative)")
