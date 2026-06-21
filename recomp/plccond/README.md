# recomp/plccond — `libplccond.so` leaf subset → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the pure-integer subset of
the proprietary i386 `libplccond.so` (PLC condition-expression evaluator).

## Verified functions (8)

| Function | Kind |
|---|---|
| `toupper_ASCII`, `tolower_ASCII` | locale-independent ASCII case fold |
| `IsPathSep` | `'/'` or `'\\'` predicate |
| `isNull` | operand is empty or all-`'0'` |
| `IsStackEmpty`, `PeekStack`, `PushStack`, `PopStack` | fixed-capacity uint16 operand **stack** over a caller flat buffer `{int top; uint16 data[513]}` (element i at byte i*2+4) |

The stack functions are **stateful** (like `recomp/plcbin`): the harness drives a
deterministic push/pop/peek/empty sequence — including the 513-element capacity
boundary and a randomised interleave — over a crafted buffer that evolves
identically on both sides.

## Proof

`build_and_verify_plccond.sh`: same 607-line harness (`verify_plccond.c`) run as
real i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256).
Oracle technique as in [`../errplib`](../errplib/README.md); the stub here carries
soname `libjhvolume.so.1` to satisfy the `JHVOLUMELIB_500.0` version requirement.
