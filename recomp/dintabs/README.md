# recomp/dintabs — libEp90_Dintabs → native ARM64, proven equivalent

A second worked instance of the project goal (after `recomp/` for libhdhinput):
decompile a proprietary HEIDENHAIN i386 control library and **recompile it to
native Apple-Silicon ARM64**, then *prove* the ARM64 build is byte-identical to
the original.

`libEp90_Dintabs.so` holds HEIDENHAIN's DIN/ANSI thread-standard tables and the
pure lookup/compare functions over them — the closest analog to libhdhinput
(pure computation, numeric I/O, no C++ state).

## What's here

| File | What it is |
|---|---|
| `extract_tables.py` | Lifts the exact static-table bytes (thread Ø, pitch, undercut, tolerance tables + scalar constants) from the real `.so`, resolving each vaddr→file-offset via the ELF section headers. Emits `dintabs_tables.h`. Verbatim bytes ⇒ no float round-trip error. |
| `dintabs_tables.h` | Auto-generated table bytes (do not hand-edit). |
| `libEp90_Dintabs_rebuilt.c` | Native re-implementation of the 7 pure functions: `GetNennd`, `hole_din_werte_freistich_{ab,cd,ef,g}`, `NenndTblVgl`. Exported under the original C++-mangled names ⇒ drop-in. |
| `libEp90_Dintabs_arm64.dylib` | Recompiled lib as native macOS arm64 Mach-O. |
| `libEp90_Dintabs_aarch64.so` | Recompiled lib as native aarch64-Linux ELF. |
| `verify_dintabs.c` | One harness, compiled two ways; prints doubles as raw IEEE-754 bit patterns for exact diffing. |
| `build_and_verify_dintabs.sh` | Reproduces the build + differential proof end to end. |

## The proof

`build_and_verify_dintabs.sh` runs the **same** 7444-line test sweep through:

1. **truth** — the real proprietary i386 `libEp90_Dintabs.so` under `qemu-i386`
   inside the ARM64 VM;
2. **rebuilt** — the native ARM64 recompile, run directly on the M2.

Result: `IDENTICAL`, same SHA-256.

Coverage: every valid `(thread-type, index)` for `GetNennd`; all four freistich
table scans over 1801 diameters (`k/20.0`, which lands on every table key
exactly — so the tolerance/`<=` comparisons are immune to x87-vs-SSE rounding)
plus out-of-grid probes; and `NenndTblVgl` scenarios.

## Why it's exact despite x87 vs SSE

The original runs FP on the i386 x87 stack (80-bit intermediates); the ARM64
rebuild uses 64-bit SSE/NEON. This matters only when a *computed* value is
observed or a comparison sits on a rounding boundary. Here:

- `GetNennd` does **no** arithmetic (pure table load) ⇒ identical unconditionally.
- The freistich scans only **compare** (`|x−key|<tol`, `key<=x`); probes hit keys
  exactly (diff 0) so the branch outcome is unambiguous on both.
- `NenndTblVgl` returns a **copied table entry**, never a computed value; inputs
  use wide margins so the branch decisions match.

## Honest scope

Same as `recomp/`: this works because these are **pure leaf** functions. It does
not generalise to the coupled C++ product. The value is twofold — it shows the
decompile→recompile pipeline is repeatable and verifiable across a *second,
FP-heavy* library, and `libEp90_Dintabs_aarch64.so` is a genuine native-ARM64
piece for the userland-translation port.
