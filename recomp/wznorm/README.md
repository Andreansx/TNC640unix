# recomp/wznorm — `libEp90_Wznorm.so` leaf subset → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the pure-integer subset of
the proprietary i386 `libEp90_Wznorm.so` (HEIDENHAIN EP90 tool-normalisation lib).

## Verified functions (5)

| Function | Kind |
|---|---|
| `GeotecToIntWkzTyp`, `IntToGeotecWkzTyp` | packed "Geotec" tool-type code ↔ decimal int (signed div/mod) |
| `AsciiToGeotecWkzTyp` | decimal string → Geotec code (libc `strtol`, 32-bit wraparound) |
| `WerkzeugTyp` | decompose a tool struct field (+0xd8) into main/sub/variant |
| `WZ_IsAussenWkz` | "is outer tool" predicate (switch over the decode) |

The i386 originals use magic-number division (`0x51eb851f`/100, `0x66666667`/10);
C signed `/`,`%` are truncation-toward-zero on both i386 and ARM64, so the clean
integer reproductions are byte-identical by construction. `AsciiToGeotecWkzTyp`
is verified over int32-range decimal strings (strtol overflow past INT32 is the
documented out-of-scope case — i386 `long` is 32-bit).

## Proof

`build_and_verify_wznorm.sh`: same 11042-line harness (`verify_wznorm.c`) run as
real i386 `.so` under `qemu-i386` vs the native ARM64 recompile → **IDENTICAL**
(same SHA-256). Oracle technique as in [`../errplib`](../errplib/README.md)
(trim NEEDED + stub + neuter ctors; no version requirement here).
