# recomp/dmathe — `libEp90_Dm` 2D-geometry math leaves → native ARM64, *behaviorally* equivalent

The `dmathe_*` family is the textbook **"computed FP / libm"** class that was
held out of the byte-identical proof: x87 80-bit intermediates vs ARM 64-bit,
plus `atan`/`sqrt`/`modf`. Reimplemented natively and proven **observably
equivalent** to the proprietary i386 `.so` under `qemu-i386`.

## Reimplemented functions (22 — the complete dmathe_* family)

| Function | Behavior |
|---|---|
| `dmathe_TauscheD(*a,*b)` | swap two doubles |
| `dmathe_RightPerpVect(x,y,*o)` | `o = {-x, y}` |
| `dmathe_LeftPerpVect(x,y,*o)` | `o = {x, -y}` |
| `dmathe_PunktDrehen(*p,…)` | rotate/translate a point in place |
| `dmathe_roundst(x,n)` | round `x·n` half-away-from-zero, `/n` |
| `dmathe_NormWinkel(a)` | normalize angle into `[0,2π)` with ε-snap |
| `dmathe_Wirein(a)` | wrap angle into `(-π,π]` |
| `dmathe_VectorWinkel(x,y)` | direction angle via `atan` |
| `dmathe_Distance(x1,y1,x2,y2)` | euclidean distance (0 inside ε-box) |
| `dmathe_Turn180Degree(*v,a)` | negate `v`, advance angle by π mod 2π |
| `dmathe_CalcOeffWinkel(a,b,flag)` | opening angle |
| `dmathe_QuadGl(a,b,c,*x1,*x2)` | quadratic solver; returns root count |
| `dmathe_InIntervall(a,b,c)` | interval test (bool) |
| `dmathe_wlinks(a,b)` / `dmathe_wrechts(a,b)` | angle left/right of (bool) |
| `dmathe_antiparallel(a,b)` | directions opposite mod 2π (bool; composes `NormWinkel`) |
| `dmathe_Winkelstrecke(x1,y1,x2,y2)` | direction angle of a segment (atan) |
| `dmathe_SpGreater0(a,b,c,d,e,f)` | sign of a dot product, degenerate fallback (bool) |
| `dmathe_RadAufBogen(start,end,dir,p)` | is angle `p` on the arc `[start,end]` (bool, 2π-wrap) |
| `dmathe_PktAufStrecke(px,py,ax,ay,bx,by)` | is point on segment a–b within ε (bool) |
| `dmathe_KreisTangentenWinkel(flag,x1,y1,x2,y2)` | circle tangent angle: `NormWinkel(Winkelstrecke ± π/2)` (sign confirmed from disasm) |
| `dmathe_PktAufBogen(px,py,cx,cy,startA,endA,dir)` | is point on a circular arc (bool): `RadAufBogen(startA,endA,dir, Winkelstrecke(cx,cy,px,py))` — recovered from disasm (Ghidra mis-typed it `void`; it tail-returns the predicate) |

All 14 FP constants (π/2, π, 2π, 3π/2, the ε's, and the float-typed 0.5/0.25/
1.0/−0.5) are lifted bit-exact from `.rodata`; the irrational ones are embedded
as exact hex-float literals so no compile-time rounding perturbs them.

## Proof — tolerant comparator

`build_and_verify_dmathe.sh` runs **12356** vectors through the real i386 `.so`
(heavy `DT_NEEDED` trimmed, `HEROSLIB_500.0` satisfied by a `libheros.so.1`-soname
stub, ctors neutered) under `qemu-i386` and the native ARM64 recompile;
`compare_dmathe.py` requires exact ints/bools and doubles within 64 ULP **or**
1e-12 relative, with a 1e-9 absolute floor for near-zero cancellation residuals.
Measured:

```
int/bool results: 7303 (exact)   double results: 5053
worst double divergence: 0 ULP, 0.00e+00 relative
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```

> `PktAufBogen` is the standout recovery: Ghidra decompiled it as a `void`
> function that calls `RadAufBogen` and throws the result away. The raw
> disassembly shows it actually **tail-returns** that boolean (the trailing
> `add esp,0x34; ret` leaves `eax` untouched), and the stack-slot shuffles reveal
> the real 6-double + flag signature. Reconstructed from the disassembly, it
> matches the real `.so` across 4050 point-on-arc cases (a genuine 2106/1944
> false/true split).

The only divergences in the entire sweep were a handful of **sub-1e-16
catastrophic-cancellation residuals** (e.g. `−5.5e-17` vs `−1.1e-16`) — values
that are mathematically zero and differ only in which denormal-scale rounding
each FPU produced. Every genuinely non-zero result matched to **0 ULP**: qemu's
x87 emulation and ARM libm round these identically. The 64-ULP tolerance is the
*methodology* a behavioral proof is entitled to, not a bound the result needed.

## `_Bool` reads

The boolean predicates (`InIntervall`, `wrechts`, `antiparallel`, …) compile to
`CONCAT31(<junk>, <bool>)` on i386; per the C++/C ABI only the low byte is
defined, so the harness reads them as `_Bool`.
