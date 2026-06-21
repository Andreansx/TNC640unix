#!/usr/bin/env python3
"""fpdiff.py <truth> <recomp> [rel_tol_exp] — tolerant verify comparison.
Differing tokens that decode as IEEE754 doubles (16-hex-digit bit patterns or
decimal floats) pass if |a-b|<=abs_tol OR rel<=rel_tol; everything else must be
exact. Real x87 (oracle) vs SSE (recomp) differ by a few ULP on computed
geometry — that is genuine FP-model divergence, ~1e-15 relative, not a bug.
Reports exact/within_tol/fail, max ULP, and the TRUE max relative error.
Exit 0 iff every line is exact-or-within-tol. rel_tol default 1e-12."""
import sys, struct

def hex2d(h):  return struct.unpack("<d", struct.pack("<Q", int(h, 16)))[0]
def is_hex64(t): return len(t) == 16 and all(c in "0123456789abcdefABCDEF" for c in t)
def ulp(ha, hb):
    a = int(ha, 16); b = int(hb, 16)
    ka = a ^ 0x8000000000000000 if a & 0x8000000000000000 else a | 0x8000000000000000
    kb = b ^ 0x8000000000000000 if b & 0x8000000000000000 else b | 0x8000000000000000
    return abs(ka - kb)
def asfloat(t):
    if is_hex64(t): return hex2d(t)
    try: return float(t)
    except ValueError: return None

def main(fa, fb, rel_tol=1e-12, abs_tol=1e-12):
    A = open(fa).read().splitlines(); B = open(fb).read().splitlines()
    if len(A) != len(B):
        print(f"  LINE COUNT DIFF truth={len(A)} recomp={len(B)}"); return 1
    exact = within = fail = 0; maxu = 0; maxrel = 0.0; fails = []
    for i, (la, lb) in enumerate(zip(A, B)):
        if la == lb: exact += 1; continue
        ta, tb = la.split(), lb.split()
        if len(ta) != len(tb): fail += 1; fails.append((i, la, lb)); continue
        ok = True
        for xa, xb in zip(ta, tb):
            if xa == xb: continue
            if is_hex64(xa) and is_hex64(xb):
                u = ulp(xa, xb)
                if u < (1 << 32): maxu = max(maxu, u)
            da, db = asfloat(xa), asfloat(xb)
            if da is not None and db is not None:
                d = abs(da - db); rel = d / max(abs(da), abs(db), 1e-300)
                if d <= abs_tol or rel <= rel_tol:
                    if not (d <= abs_tol and max(abs(da), abs(db)) < 1e-12):
                        maxrel = max(maxrel, rel)
                    continue
            ok = False; break
        if ok: within += 1
        else: fail += 1; fails.append((i, la, lb))
    print(f"  exact={exact} within_tol={within} fail={fail} max_ulp={maxu} max_rel={maxrel:.2e} (of {len(A)})")
    for i, la, lb in fails[:6]:
        print(f"   L{i}: {la}\n        {lb}")
    return 0 if fail == 0 else 1

if __name__ == "__main__":
    rt = float(f"1e-{sys.argv[3]}") if len(sys.argv) > 3 else 1e-12
    sys.exit(main(sys.argv[1], sys.argv[2], rt))
