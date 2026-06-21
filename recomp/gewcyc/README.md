# recomp/gewcyc — `libEp90_Gewcyc` thread-cycle leaves → native ARM64, *behaviorally* equivalent

Six leaf functions of `libEp90_Gewcyc.so` (thread-cycle geometry),
reimplemented natively and proven **observably equivalent** to the proprietary
i386 `.so` under `qemu-i386`.

## Reimplemented functions (6)

| Function | Behavior |
|---|---|
| `GCYC_Geostart(g, *ox, *oy)` | return the geotec's start or end point, swapped by a direction flag reached via a **pointer chase** (`*(g+0x14))+0x7b == 1`) |
| `GCYC_Geoziel(g, *ox, *oy)` | the inverse of `GCYC_Geostart` |
| `GCYC_SetInkSteigung(g, *pitch, *flags, eps)` | read the incremental pitch from the `+0x10` sub-descriptor (`+0x60`), output it, and set/clear flag bit `0x200` by `\|pitch\| ≷ eps` |
| `GCYC_LeseAltko(g, *o1, *o2, *o3)` | read up to three alt-coordinates from the `+0x10` sub-descriptor, each gated by a flag bit (`2`/`4`/`0x40`) at `+0xa0`; `0` where clear/null |
| `GCYC_Hole_Spandaten(list, idx, *o1..*o5)` | walk the `span` list (next `+4`) to the 1-based `idx`-th node, then read 5 doubles |
| `GCYC_SimpelAbhebeWinkel(flag)` | lift-off angle: `0x20→3π/2`, `1→π`, `4`/`0x100000→0`, else `π/2` |

The geotec carries **two** chased sub-descriptors — a direction descriptor at
`+0x14` and a coordinate descriptor at `+0x10` — both modeled per-arch.

`GCYC_Geostart`/`GCYC_Geoziel` chase a pointer inside the geotec (the direction
descriptor at `+0x14`, flag byte at `+0x7b`), so a shared flat buffer won't work
(4- vs 8-byte pointers). `gewcyc_layout.h` mirrors the field order; the harness
builds the geotec **per-arch** from identical logical inputs (flag, start/end
points). Constants from `.rodata`: `DAT_24c40=π/2`, `DAT_24c50=π`,
`DAT_24c90=3π/2`. All three are C++ symbols bound via `__asm__` labels.

## Proof

`build_and_verify_gewcyc.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS refs
auto-stubbed, ctors neutered; no proprietary `VERNEED`) runs 310 vectors → 0 ULP:

```
double results: 310   worst double divergence: 0 ULP, 0.00e+00 relative
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```
