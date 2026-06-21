# recomp/bohrcyc_fp — FP geometry leaves → native ARM64, *behaviorally* equivalent

Where the [`../bohrcyc`](../bohrcyc/) directory proved the **integer** subset of
`libEp90_Bohrcyc.so` byte-identical, this directory takes the two
**floating-point** leaves that were *excluded* from that bar — they cross the
x87-vs-SSE / transcendental boundary, so their machine code cannot be made
bit-identical across the ISA. Instead we prove **behavioral (observable)
equivalence**: same return codes (exactly) and same `double` outputs to within a
tight FP tolerance, differentially against the genuine i386 `.so` under
`qemu-i386`.

## Reimplemented functions (2)

| Function | Behavior |
|---|---|
| `BCYC_EntnormiereWinkel(double *w, double ref, double half)` | angle de-normalization: if `|*w−ref| ≥ half+π`, shift `*w` by ±2π toward `ref` and return 1; else return 0. The two FP constants (π, 2π) are lifted bit-exact from `.rodata` (`0x400921fb54442d18`, `0x401921fb54442d18`). |
| `BCYC_WinkelGleich(double a, double b, double tol)` | angles-equal test: `|sin a−sin b| < tol && |cos a−cos b| < tol` (wrap-insensitive). Uses `sincos` in the original; we call `sin`/`cos` separately (identical pair on a platform, portable across macOS clang / Linux gcc). |

## Proof — tolerant comparator

`build_and_verify_bohrcyc_fp.sh` runs the same **70,957** vectors through the real
i386 `.so` (trimmed to libc+libm, loaded standalone under `qemu-i386`) and the
native ARM64 recompile, then `compare_fp.py` checks:
- integer/boolean **return codes match exactly**;
- `double` results match within **≤2 ULP** (or abs ≤ 1e-12);
- a flipped `WinkelGleich` boolean is tolerated *only* at an ULP-level threshold
  coin-flip, and reported.

Measured result:

```
ENT vectors: 12393   max value diff: 0 ULP
WG  vectors: 58564   boundary coin-flips tolerated: 0
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```

In practice the residual was **0 ULP** across the whole sweep — qemu's x87
emulation rounds these add/sub operations identically and the `sincos`
comparison margins are well-separated from the tolerance — so the observable
behavior is exact. The tolerance in the comparator is the *methodology* (what a
behavioral proof is allowed to accept), not a crutch the result needed.

## Why this is "behavioral", not "byte-identical"

The `.text` here genuinely differs from the i386 original: the original emits
x87 `fld`/`fsincos`/`fstp`; the ARM64 build emits `fmul`/`bl sin`/`fcmp`. We do
**not** claim the bytes match — we claim the *function* matches: identical
outputs for identical inputs across the tested domain. That is the right and
honest bar for code that computes transcendental math.
