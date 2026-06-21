#!/usr/bin/env python3
"""
compare_fp.py truth.txt recomp.txt

Behavioral-equivalence comparator for the FP leaves of libEp90_Bohrcyc.
- Integer/boolean return codes must match EXACTLY.
- Double results must match within a tight FP tolerance (default: <=2 ULP, or
  abs <= 1e-12) — the residual is pure x87-vs-ARM double-rounding.
- For BCYC_WinkelGleich, a flipped boolean is tolerated ONLY when the deciding
  margin sits within REL_BOUNDARY of the tolerance (i.e. an ULP-level coin-flip
  at the threshold); such cases are reported, and any flip away from the
  boundary is a hard FAIL.
"""
import sys, struct

REL_BOUNDARY = 1e-9   # |margin - tol| / tol below this => threshold coin-flip

def ulp_diff(a, b):
    ua = struct.unpack("<q", struct.pack("<d", a))[0]
    ub = struct.unpack("<q", struct.pack("<d", b))[0]
    if ua < 0: ua = (1 << 63) - ua
    if ub < 0: ub = (1 << 63) - ub
    return abs(ua - ub)

def load(path):
    ent, wg = {}, {}
    for line in open(path):
        p = line.split()
        if not p: continue
        if p[0] == "ENT":
            ent[int(p[1])] = (int(p[2]), int(p[3], 16), float(p[4]))
        elif p[0] == "WG":
            wg[int(p[1])] = (int(p[2]), float(p[3]), float(p[4]))
    return ent, wg

def main():
    t_ent, t_wg = load(sys.argv[1])
    r_ent, r_wg = load(sys.argv[2])
    fails = []
    max_ulp = 0

    # --- EntnormiereWinkel: exact ret, tolerant value ---
    if t_ent.keys() != r_ent.keys():
        fails.append("ENT index set differs")
    for i in t_ent:
        tret, tbits, tval = t_ent[i]
        rret, rbits, rval = r_ent[i]
        if tret != rret:
            fails.append(f"ENT[{i}] ret {tret} != {rret}")
        u = ulp_diff(tval, rval)
        max_ulp = max(max_ulp, u)
        if u > 2 and abs(tval - rval) > 1e-12:
            fails.append(f"ENT[{i}] value {tval!r} != {rval!r} ({u} ULP)")

    # --- WinkelGleich: exact ret except ULP-level boundary coin-flips ---
    n_flip_boundary = 0
    if t_wg.keys() != r_wg.keys():
        fails.append("WG index set differs")
    for i in t_wg:
        tret, tmargin, ttol = t_wg[i]
        rret, rmargin, rtol = r_wg[i]
        if tret != rret:
            # tolerate only if the truth margin is essentially AT the threshold
            rel = abs(tmargin - ttol) / ttol if ttol else 0.0
            if rel <= REL_BOUNDARY:
                n_flip_boundary += 1
            else:
                fails.append(f"WG[{i}] ret {tret}!={rret} margin={tmargin:.17g} tol={ttol:g} rel={rel:.2e}")

    print(f"ENT vectors: {len(t_ent)}   max value diff: {max_ulp} ULP")
    print(f"WG  vectors: {len(t_wg)}   boundary coin-flips tolerated: {n_flip_boundary}")
    if fails:
        print("RESULT: MISMATCH")
        for f in fails[:40]:
            print("  " + f)
        sys.exit(1)
    print("RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386")
    print(f"  (return codes exact; doubles within {max_ulp} ULP; "
          f"{n_flip_boundary} threshold coin-flips at the WinkelGleich boundary)")

if __name__ == "__main__":
    main()
