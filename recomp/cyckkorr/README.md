# recomp/cyckkorr — `libEp90_Cyckkorr` leaves → native ARM64, *behaviorally* equivalent

Two leaf functions of `libEp90_Cyckkorr.so` (cycle-correction geometry),
reimplemented natively and proven **observably equivalent** to the proprietary
i386 `.so` under `qemu-i386`.

## Reimplemented functions (2)

| Function | Behavior |
|---|---|
| `renormiere_punkt(*px, *py, quad, flag)` | rotate the point by a quadrant (0/90/180/270°) using the **bit-exact ~1e-16 FP residuals** of `sin 180°` / `cos 90°` / `sin 270°`; two `flag` sign conventions; out-of-range `quad` is a no-op |
| `ckk_uebertrage_attribute(src, dst)` | copy a fixed set of attribute fields (`+0x64`, `+0x6c`, `+0x6d`, `+0x70`, `+0x78`, `+0x7c`, `+0x84`, `+0x88`) between two geotec elements (flat struct) |

`renormiere_punkt`'s rotation constants are lifted verbatim from `.rodata`
(`DAT_42d48 = 1.2246e-16`, `DAT_42d40 = 6.123e-17`, `DAT_42d38 = -1.837e-16`) —
the actual floating-point sin/cos values at the right angles, not idealized
zeros, so the rotation reproduces the original's exact arithmetic. `hsr_at` (the
`quad`) is a small enum passed by value as a 4-byte argument; both functions are
C++ symbols bound via `__asm__` labels.

## Proof

`build_and_verify_cyckkorr.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS refs
auto-stubbed, ctors neutered; no proprietary `VERNEED`) runs **1340** vectors → 0 ULP:

```
int/bool results: 44 (exact)   double results: 1296   worst divergence: 0 ULP
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```

(The local `chk_hinterschneidung(akopf*, double)` undercut-walker is a clean
`__regparm1` list-chaser but a **local** symbol — not exported in `.dynsym`, so it
can't serve as the differential oracle and is excluded.)
