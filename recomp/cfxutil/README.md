# recomp/cfxutil — `libConvertCfxNCK.so` text/number utils → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the pure-integer text/number
leaf utilities exported by the proprietary i386 `libConvertCfxNCK.so` (these same
helpers also appear in `libKinematicsDesign_sl`, etc.).

## Verified functions (4)

| Function | Kind |
|---|---|
| `IsBinNumber` | is `s` an optional-`'%'`-prefixed string of only `'0'`/`'1'` |
| `BinAtol` | parse such a binary string → 32-bit value (spaces skipped; 32-bit wrap) |
| `IsUtf8` | does `s` start with the UTF-8 BOM (`EF BB BF`) |
| `utf16_strlen` | length of a NUL-terminated uint16 string |

All are call-free string scanners. (`HexAtol` is excluded — it calls libc locale
`ctype`, so it isn't a pure leaf.) Note the deliberate semantic split the proof
captures: `IsBinNumber("1 0 1")` = 0 (rejects spaces) yet `BinAtol("1 0 1")` = 5
(ignores them), and 32 `'1'` chars → `0xffffffff` (i386 `ecx` wraps at 32 bits).

## Proof

`build_and_verify_cfxutil.sh`: same 43-line harness (`verify_cfxutil.c`) run as
real i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256).
Oracle technique as in [`../errplib`](../errplib/README.md) (stub soname
`libjhvolume.so.1` for the `JHVOLUMELIB_500.0` version requirement).
