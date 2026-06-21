# recomp/file — `libfile.so` leaf subset → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the exported pure-integer
leaf subset of the proprietary i386 `libfile.so` (HeROS file layer).

## Verified functions (5 exported)

| Function | Kind |
|---|---|
| `BitFieldTst` | test bit `n` of a byte array (signed byte index → `sar 3`, `movsx`) |
| `IsNcFile` | NC file-type tag predicate (`{4,5}` or `{0x26,0x27}`) |
| `IsAscFile` | ASCII file-type tag membership (i386 jumptable) |
| `FlServerListSize` | server-list header count `@+4` |
| `read_mminch` | constant `0` |

`FlModAccess` is a pure leaf too but a **local** symbol (not in `.dynsym`), so it
can't be the truth oracle and is left out of the verified set.

## Proof

`build_and_verify_file.sh`: same 85-line harness (`verify_file.c`) run as real
i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256).
Oracle technique as in [`../errplib`](../errplib/README.md) (stub soname
`libheros.so.1` for the `HEROSLIB_500.0` version requirement).
