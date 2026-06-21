# recomp/drehcyc — `libEp90_Drehcyc` leaf → native ARM64, *behaviorally* equivalent

`is_aufmass_aktiv` — a self-contained leaf of `libEp90_Drehcyc.so` (turning-cycle
geometry) — reimplemented natively and proven **observably equivalent** to the
proprietary i386 `.so` under `qemu-i386`.

## Reimplemented function (1)

| Function | Behavior |
|---|---|
| `is_aufmass_aktiv(am, tol)` | is a machining allowance active? `(am.flag & 0x30)==0 → false`; `\|am.m1\| < tol → (tol ≤ \|am.m2\|)`; else `true` |

The notable wrinkle: `aufmass_rt` is passed **by value**. The disassembly
(`0xbcb0`) gives its by-value field offsets (`flag @+0`, `m1 @+4`, `m2 @+0xc` —
i386 4-byte double alignment); `drehcyc_layout.h` declares the struct so each
arch's by-value calling convention carries identical logical content. The bool
return is read as `_Bool` (the i386 `setbe al` leaves the upper `eax` undefined).

## Why only one function

`libEp90_Drehcyc` is a **function-pointer-table architecture**: most of its 67
"call-free" exports are runtime-registered **forwarder thunks** (`jmp *GOT[...]`)
whose real bodies the host supplies at load time — not reimplementable
standalone. `is_aufmass_aktiv` is one of the genuine self-contained leaves
(filtered by "has an x87 body, no indirect `jmp`/`call`").

## Proof

`build_and_verify_drehcyc.sh` runs 700 deterministic vectors → plain `diff`:

```
truth lines: 700   recomp lines: 700
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases
49a892ff…  truth.txt
49a892ff…  recomp.txt      # same SHA-256
```
