# recomp/metaval — `libtncMetaValue` C++ class methods → native ARM64, *behaviorally* equivalent

The first **C++ class-method** target. Where the byte-identical directories
proved pure C leaves, these are genuine C++ member functions — mangled
Itanium-ABI symbols with a `this` pointer, returning `bool`/`double`/`int`.
They cannot be byte-identical across the ISA boundary (different ABIs, x87 vs
SSE return registers, `this`-layout pointer widths), so we prove **observable
equivalence**: identical outputs for identical logical inputs, differentially
against the genuine i386 `.so` under `qemu-i386`.

## Reimplemented methods (15)

**Static (no `this`) — unit conversions:**

| Method | Behavior |
|---|---|
| `TncMetaValue::InchPrecision(bool b, double d)` | `b && 1.0 < d` |
| `TncMetaValue::ToMetricFeedValue(bool m)` | `m ? 2.54 : 1.0` |
| `TncMetaValue::ToMetricPosValue(bool m)` | `m ? 25.4 : 1.0` |
| `TncMetaValue::ToNonMetricFeedValue(bool m)` | `1 / (m ? 2.54 : 1.0)` |
| `TncMetaValue::ToNonMetricPosValue(bool m)` | `1 / (m ? 25.4 : 1.0)` |

The two factors (2.54, 25.4) are lifted bit-exact from `.rodata` (file offsets
`0x84a0`/`0x84a8`).

**`CycMetaValue` accessors (`this`, in-object fields):**

| Method | Behavior |
|---|---|
| `IsCardinal` / `IsInteger` | `0` (shared code at the same address) |
| `IsReal` | `1` |
| `GetArraySize` | `1` |
| `IsSigned` | `*(double*)(this+0x0c) < 0` |
| `GetTextLength` | `*(uint32*)(this+0x24)` |

**`TncMetaValue` pImpl accessors (`this`, pointer indirection):**

| Method | Behavior |
|---|---|
| `IsSigned` | `impl ? ((fmt->flags>>5)&1) : 0` |
| `IsSignedQ` | `(impl && fmt->type==6) ? ((fmt->flags>>5)&1) : 0` |
| `GetTextLength` | `fmt->textlen` |
| `GetArraySize` | `impl != 0` |

## How the `this`-layout problem is solved

`TncMetaValue` is a pImpl object holding pointers (`fmt`, `impl`). A flat shared
byte buffer won't work — i386 pointers are 4 bytes, ARM64 pointers 8. So
`metaval_layout.h` mirrors the class *field order* and the harness builds the
object **per-arch from identical logical inputs** (impl-present, `fmt->flags`,
`fmt->type`, `fmt->textlen`). Compiled for i386 it reproduces the original byte
offsets (`fmt@+4`, `impl@+8`); compiled for ARM64 it lays out native 8-byte
pointers. Both the real method and the reimplementation then read their own
arch's correct offsets. `CycMetaValue`'s fields are *inside* the object (no
indirection), so those use a flat buffer directly.

## The `bool` register-leak detail

`CycMetaValue::IsSigned` compiles (i386) to `fcomi` + `setb al`, leaving the
upper 24 bits of `eax` holding **load-address-dependent garbage**
(`0x408260xx`). Per the C++ ABI a `bool` return defines only `al`, so the
harness reads these methods as `_Bool` (low byte) — comparing wider would test
non-deterministic PIC bits, not behavior. This is the honest contract: the
function returns a boolean; the boolean matches.

## Proof

`build_and_verify_metaval.sh` runs **1283** vectors through the real i386 `.so`
(heavy `DT_NEEDED` trimmed, unversioned HeROS refs satisfied by an
auto-generated stub, C++ static ctors neutered) under `qemu-i386` and the native
ARM64 recompile; `compare_metaval.py` requires exact integer/boolean results and
doubles within ≤2 ULP. Measured:

```
int results: 1275 (exact)   double results: 8  max 0 ULP
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386
```

## Excluded (genuinely not leaves)

The `TncMetaValue` `Is.../Get...` members that dispatch **virtually** through the
pImpl vtable (`IsInteger`, `IsReal`, `IsCardinal`, `IsParameter`,
`GetMemorySize`) require a live impl object's vtable and cannot be reproduced
standalone.
