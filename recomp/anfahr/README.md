# recomp/anfahr — `libEp90_Anfahr` FP leaves → native ARM64, *behaviorally* equivalent

Two pure-FP leaves of `libEp90_Anfahr.so` (approach-motion geometry),
reimplemented natively and proven **observably equivalent** to the proprietary
i386 `.so` under `qemu-i386`.

## Reimplemented functions (2)

| Function | Behavior |
|---|---|
| `EckenWinkel(a1,a2,eckeType)` | corner angle: `0x20 → π`; otherwise `\|a1-a2\|`, folded to `2π-\|a1-a2\|` when it's on the wrong side of π (direction chosen by `eckeType ≷ 0x20`) |
| `get_einfahr_radius(p1,p2,p3)` | entry-radius clamp (C=3, D=5): `p1≤p3 → min(5, 3·p2)`; `p1 < p3+3·p2 → min(2·p1, 3·p2)`; else `p1` |

## `get_einfahr_radius` — a disassembly recovery

Like `dmathe_PktAufBogen`, Ghidra decompiled this as a **`void`** function whose
branches all `return;` — it lost the x87 return value. Tracing `st0` through the
real `.text` (`fldl`/`fmull`/`fcomi`/`fcmovnbe`/`fstp` at `0x3a20`) reconstructs
the real three-way result. The subtlety that first bit: the fall-through
`fstp st(1)` leaves **`p1`** in `st0` after the pop (not `C·p2`) — caught and
fixed by the differential test. `eckeType` is an `ecke_st` enum passed as a
4-byte value; both functions are C++ symbols bound via `__asm__` labels.

## Proof

`build_and_verify_anfahr.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS refs
auto-stubbed, ctors neutered; no proprietary `VERNEED`) runs **22 669** vectors
through the real i386 `.so` and the native ARM64 recompile; `compare_anfahr.py`
requires exact ints and doubles within tolerance. Measured:

```
double results: 22669   worst double divergence: 0 ULP, 0.00e+00 relative
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```
