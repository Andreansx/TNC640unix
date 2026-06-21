# recomp/bohrcyc — libEp90_Bohrcyc pure-integer subset → native ARM64, proven equivalent

A fourth worked instance — and the first **partial** (cherry-picked) one. It
demonstrates that when a library is *not* a leaf as a whole, you can still lift
and verify its pure-integer leaf functions.

`libEp90_Bohrcyc.so` is HEIDENHAIN's drilling-cycle library. Most of it is **not**
byte-identical-recompilable: it does FP geometry (x87 `sin`/`sqrt`, divergent from
ARM64 NEON and a different libm) and calls out to the Ep90 geometry libs
(`Fflib`/`Gtlib`/`Geolib`). Its **pure-integer accessors**, however, are exact:

| Function | What it does |
|---|---|
| `BCYC_Typisiere_Werkzeug(int, int*, unsigned*)` | tool type = `arg>>8`, sub-type = `(arg>>4)&0xf` |
| `BCYC_Angetr_Werkz(void*)` | driven-tool predicate over the tool byte at `+0xe0` |

## The proof

`build_and_verify_bohrcyc.sh` runs the same harness two ways (real i386 under
`qemu-i386` vs native ARM64) over 2258 vectors: `Typisiere_Werkzeug` across the
full 32-bit input range (including negatives), and `Angetr_Werkz` over all 256
tool-byte values. Result: `IDENTICAL`, same SHA-256.

`Angetr_Werkz` is a nice demonstration of *bit-faithful* recompilation: its i386
code uses `setbe al` / `sete dl`, which write only the **low byte** of the result
register, so the upper bytes of `(t-1)` "leak" into the 32-bit return value. That
deterministic leakage is reproduced exactly — a reminder that matching the
*observable* return requires the disassembly, not just the pseudo-C.

The oracle is a `patchelf`-trimmed copy of the real `.so` (Ep90 geometry deps
removed; the two functions reference no external symbols), same technique as
`recomp/plcbin/`.

## Honest scope

This is explicitly the **integer-pure subset** of an otherwise FP/coupled
library — not a whole-lib recompile. The excluded functions:

- `BCYC_WinkelGleich` — uses `sincos`; libm results differ across implementations.
- `BCYC_EntnormiereWinkel` — pure (works in radians, ±2π) but produces a
  *computed* FP output, so x87 80-bit→64-bit double-rounding can differ from
  ARM64's direct 64-bit rounding on rare inputs. Excluded to keep the
  byte-identical claim unconditional.

See `docs/16-arm64-decompilation-and-translation.md §3a` for where this sits in
the overall picture.
