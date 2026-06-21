# recomp/plcmap — `libplcmap.so` leaf subset → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the exported pure-integer
helpers of the proprietary i386 `libplcmap.so` (PLC I/O symbol map).

## Verified functions (4 exported)

| Function | Kind |
|---|---|
| `Swap_d`, `Swap_w` | 32-/16-bit byte swap (endian) |
| `UQuadCompare` | unsigned 64-bit compare `(hi:lo)` → +1/-1/0 |
| `NumberOfCharacters` | signed-decimal print width — reproduces the i386 **INT_MIN quirk** exactly (its abs stays `0x80000000`, treated as ≤9 by the signed test, so the digit loop is skipped and it returns 2, not 11) |

`hexbyte`, `pmap_getsymbollen`, `pmap_setmode` are pure leaves too but are **local**
symbols (not in `.dynsym`), so they can't be linked as the truth oracle and are
left out of the verified set.

## Proof

`build_and_verify_plcmap.sh`: same 5921-line harness (`verify_plcmap.c`) run as
real i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256).
Oracle technique as in [`../errplib`](../errplib/README.md) (stub soname
`libjhvolume.so.1` for the `JHVOLUMELIB_500.0` version requirement).
