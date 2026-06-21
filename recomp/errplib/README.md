# recomp/errplib — `libEp90_Errplib.so` leaf subset → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the pure-leaf subset of the
proprietary i386 `libEp90_Errplib.so` (HEIDENHAIN EP90 error-handling library).

## Verified functions (12)

| Function | Kind |
|---|---|
| `ERR_IsSystemWarning/IsUserWarning/IsSystemError/IsUserError` | error-class predicate (== const) |
| `ERR_IsInternWarning/IsExternWarning/IsInternError/IsExternError` | class + 0/non-0 byte predicate |
| `ERR_IsWarning`, `ERR_IsError` | predicate that **leaks** the i386 return register (`setbe al` over `(cls-N)`) — verified as full 32-bit |
| `ERRPLIB_GetFacilityID` | 72-entry `.rodata` table lookup (table lifted verbatim, vaddr 0x3AC0) |
| `IsDPDemo` | constant `0` |

## Proof

`build_and_verify_errplib.sh` runs the **same** harness (`verify_errplib.c`, a
4232-line deterministic sweep) two ways and diffs:

1. **truth** — the real proprietary i386 `.so` under `qemu-i386`;
2. **rebuilt** — the native ARM64 recompile (`libEp90_Errplib_partial_rebuilt.c`).

Result: **IDENTICAL** (same SHA-256).

## Oracle technique

The real `.so` drags the whole HeROS runtime via `DT_NEEDED` + a `HEROSLIB_500.0`
version requirement, but the 12 leaf functions reference no external symbols. So
the genuine proprietary `.text` is loaded standalone by: (a) `patchelf
--remove-needed` the heavy deps; (b) an auto-generated stub `.so`
(`errplib_stub.c`, soname `libheros.so.1` + version script `errplib_stub.ver`)
that satisfies the residual load-time relocations and the version check; (c)
`neuter_init.py` zeroes `DT_INIT/FINI[_ARRAY]` so the C++ static ctors (which call
into the trimmed-away runtime) don't run. The `.text` of the 12 functions is
unchanged proprietary machine code.
