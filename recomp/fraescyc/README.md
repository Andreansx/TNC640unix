# recomp/fraescyc — `libEp90_Fraescyc` milling-cycle accessors → native ARM64, *behaviorally* equivalent

Three `FCYC_*` accessor leaves of `libEp90_Fraescyc.so` (milling-cycle
geometry), reimplemented natively and proven **observably equivalent** to the
proprietary i386 `.so` under `qemu-i386`. Each reads a `tec_cycfraes_rt` struct
by flat field offset (no pointer indirection).

## Reimplemented functions (3)

| Function | Behavior |
|---|---|
| `FCYC_FraesTiefe(p)` | milling depth (`+0x74`) if the element is active (`+0x10 ≠ 0`), else 0 |
| `FCYC_AbhebeLaenge(p, v)` | lift length `\|p[0x148]−v\|` if `p[0x148]` is finite-ish (`< 5e37`) and the length meets the `+0x94` threshold; else 0 |
| `FCYC_VorschubArt(p, a, b)` | feed-motion type (`0` or `2`) from `+0x16c`/`+0x170`/`+0x192` flags and the scalar inputs |

## Proof

`build_and_verify_fraescyc.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS refs
auto-stubbed, ctors neutered; no proprietary `VERNEED`) → 0 ULP:

```
int/bool results: 6912 (exact)   double results: 1152   worst divergence: 0 ULP
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```

## Excluded — `FCYC_AnzahlSchichten` (honest non-result)

The layer-count accessor converts an **x87 80-bit intermediate** to int via
`fisttpl` (*truncation*), and its exact value also depends on the
AT&T-reversed `fdivp`/`fsubp` operand order. Neither `double` nor 128-bit
`long double` on ARM reproduces the 80-bit truncation at the integer boundary
bit-exactly (off-by-one at boundary inputs; `fisttpl(inf)` yields the x87
integer-indefinite `0x80000000`, then clamps to 1). Rather than ship a
reconstruction the differential test rejects, it's excluded — the diff correctly
gates it out, which is the point.
