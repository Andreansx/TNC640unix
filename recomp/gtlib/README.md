# recomp/gtlib — `libEp90_Gtlib.so` classifiers → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the single-level
`GTFIND_Is*` feature classifiers of the proprietary i386 `libEp90_Gtlib.so`
(HEIDENHAIN EP90 geometry/Geotec library).

## Verified functions (7)

| Function | Kind |
|---|---|
| `GTFIND_IsBohrung/IsFasRun/IsFreistich/IsEinstich/IsGewinde` | geotec tag (`@+0x54`) == const → clean bool |
| `GTFIND_IsVariante` | tag == param2; **leaks** param2 into the return register (`sete al`) |
| `GTFIND_IsFigurRucksack` | `1<<tag` bitmask vs `0xd2800`; **leaks** `1<<tag` into the return register (`setne al`) |
| `GTFIND_IsYEbene/IsMantel` | plan_at type-code classifiers (arg is the code, no struct) |
| `GTFIND_IsPkt/IsTanZiel/IsDefTanZiel` | `IsVariante(p,1)` gate + a bit of geotec `+0x5c`/`+0x58`/`+0xc` |
| `GTFIND_IsCirc/IsDefCir` | `IsVariante(p,1)` gate + bit 6 of geotec `+0x5c`/`+0x58` |
| `GTFIND_IsUeberlagerung` | composite of `IsFasRun`/`IsFreistich`/`IsEinstich`/`IsBohrung` |
| `GTFIND_IsCirCW/IsCirCCW` | `IsCirc` gate, then a `double<0.0` / `0.0<double` **sign-compare** of `+0xb0` (FP comparison — byte-identical incl. -0.0/NaN/+0; no FP *arithmetic* so it stays exact) |

These are C++ symbols (typed `geotec*` params) so each is bound by its exact
mangled name (`_Z16GTFIND_IsBohrungP6geotec`, …) via an `__asm__` label on both
the harness extern and the rebuilt definition. Only **single-level** classifiers
(read one struct field) qualify — the list-walking variants dereference 32-bit
stored pointers and so can't be verified against a 64-bit buffer.

## Proof

`build_and_verify_gtlib.sh`: same 127-line harness (`verify_gtlib.c`) run as real
i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256). The
two register-leak functions are checked as a full 32-bit `unsigned`. Oracle
technique as in [`../errplib`](../errplib/README.md) (stub soname `libheros.so.1`
for the `HEROSLIB_500.0` version requirement).
