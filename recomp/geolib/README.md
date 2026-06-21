# recomp/geolib — `libEp90_Geolib` FP geometry leaves → native ARM64, *behaviorally* equivalent

The pure floating-point geometry-math leaves of `libEp90_Geolib.so` (distances,
angle normalization, and the sin/cos angle classifiers that the higher-level
`GEOLIB_Is*` predicates build on), reimplemented natively and proven **observably
equivalent** to the proprietary i386 `.so` under `qemu-i386`.

## Reimplemented functions (17)

| Function | Behavior |
|---|---|
| `abstand_pkt_pkt(x1,y1,x2,y2)` | Euclidean distance `√((x2-x1)²+(y2-y1)²)` |
| `abstand_pkt_gerade(px,py,qx,qy,angle)` | signed perpendicular distance from a point to the line through `(qx,qy)` at `angle` |
| `norm_winkel(angle, eps)` | normalize into `[0,2π)`, snap to 0 within `eps` of either end (eps is a *parameter*) |
| `compare_sinus_winkel(s1,c1,s2,c2,eps)` | classify two angles from their `(sin,cos)` pairs → `1` (same) / `3` / `5` |
| `compare_winkel(a1,a2,tol)` | classify two angles: `sincos` + `compare_sinus_winkel` with `eps=sin(tol)`; on a non-`{1,3}` result, retry against `a2+π/2` and remap → `{1,3}` or `{2,4,5}` |
| `oeffnungswinkel(a1,a2,dir,tol)` | signed opening angle from `a1` to `a2` with 2π-unwrapping chosen by `sign(dir)`; `0` if the angles compare equal |
| `GEOLIB_IsIdentisch(g1,g2,tol)` | are two geometry elements the **same**? line: `start1≈start2 ∧ end1≈end2`; arc (flag `0x40`): also `\|r1−r2\|<tol ∧ center1≈center2` |
| `GEOLIB_IsInvers(g1,g2,tol)` | is `g1` the **reverse** of `g2`? line: `start1≈end2 ∧ end1≈start2`; arc: also `\|r1+r2\|<tol ∧ center1≈center2` |
| `GEOLIB_IsMathIdentisch(g1,g2,posTol,angTol)` | same **infinite** element: line: same direction (`compare_winkel==1`) ∧ g1.start on g2's line; arc: `\|r1−r2\|<posTol ∧ centers coincide` |
| `GEOLIB_IsMathInvers(g1,g2,posTol,angTol)` | same infinite element, **opposite** direction (`compare_winkel==3`); arc uses `\|r1+r2\|` |
| `GEOLIB_IsStartpunkt(g,px,py,tol)` | is `(px,py)` the **start** of `g`? `dist(g.start,(px,py)) < tol + 2⁻¹⁵` |
| `GEOLIB_IsZielpunkt(g,px,py,tol)` | is `(px,py)` the **end** of `g`? (same, against `g.end`) |
| `GEOLIB_GetWinkelRichtung(angle,eps)` | classify a direction into one of **16 sectors** (octant centers + boundary bands), returning a power-of-2 code `{2,4,…,0x10000}`; boundaries `π/4…7π/4` with ±eps bands, recovered bit-exact |
| `GEOLIB_IsGeoringBereich(g1,g2,tol)` | does `g1`'s start coincide with `g2`'s end (and `g1≠g2`)? `dist(g1.start,g2.end) < tol` |
| `wert_im_intervall(a,b,c,tol)` | is `a` in the closed interval `[min(b,c),max(b,c)]` widened by tol (orientation-agnostic)? |
| `wert_im_offenen_intervall(a,b,c,tol)` | the open-interval variant |
| `flaeche_von_trapez(a,b,h)` | trapezoid area `0.5·(a+b)·h` |

The `GEOLIB_Is*` predicates read a geometry-element struct by **flat field
offsets** (no internal pointers — `+0x24` startX, `+0x2c` startY, `+0x5c`
type-flags, `+0x90`/`+0x98` end, `+0xb0` radius, `+0xb8`/`+0xc0` center), so a
shared flat buffer drives both arches; the harness builds line and arc elements
in identical/reversed/offset configurations. Constants from `.rodata`:
`DAT_0001ff48 = 2π`, `DAT_0001ff70 = π/2`.

## Proof

`build_and_verify_geolib.sh` runs **36 174** vectors through the real i386 `.so`
(heavy `DT_NEEDED` trimmed — `libEp90_Errplib`/`libcodecheck-registry` — unversioned
HeROS refs auto-stubbed, ctors neutered; Geolib has no proprietary `VERNEED`)
under `qemu-i386` and the native ARM64 recompile; `compare_geolib.py` requires
exact ints and doubles within 64 ULP / 1e-12 relative (1e-9 absolute floor for
cancellation). Measured:

```
int/bool results: 25695 (exact)   double results: 10479
worst double divergence: 0 ULP, 0.00e+00 relative
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```

The `compare_winkel` arg order into `compare_sinus_winkel` was recovered from the
disassembly (`eps = sin(tol)`, the `+π/2` retry) and confirmed by the
differential test on the first try — every one of the 7000+ classifier results
matched exactly.
