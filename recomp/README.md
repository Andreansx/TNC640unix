# recomp — decompile → native ARM64, proven equivalent

This directory contains a **worked, verified instance** of the project goal:
take a proprietary HEIDENHAIN i386 control library, decompile it, and **recompile
it to native Apple-Silicon ARM64 machine code** — then *prove* the ARM64 build
behaves identically to the original.

## What's here

| File | What it is |
|---|---|
| `libhdhinput_rebuilt.c` | Native re-implementation of the pure numeric-field accessor/validator functions from the proprietary i386 `libhdhinput.so`, recovered via Ghidra (`work/re/out/libhdhinput.decomp.c`) + targeted disassembly. Every offset/mask/branch preserved from the original. |
| `libhdhinput_arm64.dylib` | The recompiled library as a **native macOS Apple-Silicon arm64** Mach-O. |
| `libhdhinput_aarch64.so` | The recompiled library as a **native aarch64-Linux** ELF — the drop-in for the ARM64 VM port. |
| `verify_harness.c` | One harness, compiled two ways (i386→real `.so`, arm64→rebuilt), emits identical output for byte-diffing. |
| `build_and_verify.sh` | Reproduces the whole build + differential proof end to end. |

## The proof

`build_and_verify.sh` runs the **same** 4000 deterministic test vectors through:

1. **truth** — the *real* proprietary i386 `libhdhinput.so`, executed under
   `qemu-i386` translation inside the ARM64 Linux VM;
2. **rebuilt** — the native ARM64 recompile, executed directly on the M2.

…and compares. Current result:

```
RESULT: IDENTICAL ✓  native ARM64 == proprietary i386 on all cases
68382d99…  truth.txt
68382d99…  recomp.txt          # same SHA-256
```

The test covers all 13 exported functions and every branch, including the two
non-obvious ones the decompiler hid behind `regparm` calls (type-3 clears bit 30
of the magnitude; type-6 adds 1 when negative — both recovered from the
`check_pzt_range` jump table in the disassembly).

## More worked instances — 14 libraries, 88 functions verified byte-identical

The same decompile→recompile→prove pipeline has been replicated across many
libraries spanning different code classes:

| Dir | Library | Class | Proof |
|---|---|---|---|
| [`dintabs/`](dintabs/) | `libEp90_Dintabs.so` (7 fns) | pure FP table lookups (DIN/ANSI thread tables) | byte-identical, 7444-line sweep |
| [`plcbin/`](plcbin/) | `libplcbin.so` (5 fns) | stateful big-endian `.bin` file parser | byte-identical on a crafted module |
| [`bohrcyc/`](bohrcyc/) | `libEp90_Bohrcyc.so` (2 fns, int subset) | pure-integer accessors of an FP/coupled lib | byte-identical, 2258 vectors |
| [`errplib/`](errplib/) | `libEp90_Errplib.so` (12 fns) | error-class predicates (2 with register leak) + 72-entry facility table | byte-identical, 4232 lines |
| [`wznorm/`](wznorm/) | `libEp90_Wznorm.so` (5 fns) | tool-type codec (signed div/mod, strtol) + struct-field classifiers | byte-identical, 11042 lines |
| [`plccond/`](plccond/) | `libplccond.so` (8 fns) | ASCII helpers + a stateful fixed-capacity operand stack | byte-identical, 607 lines |
| [`gtlib/`](gtlib/) | `libEp90_Gtlib.so` (9 fns) | single-level `GTFIND_Is*` geometry classifiers (C++ mangled symbols, 2 with register leak) | byte-identical, 127 lines |
| [`plcmap/`](plcmap/) | `libplcmap.so` (4 fns) | endian/64-bit-compare/decimal-width helpers | byte-identical, 5921 lines |
| [`file/`](file/) | `libfile.so` (5 fns) | bit-array test + file-type tag predicates | byte-identical, 85 lines |
| [`winmgr/`](winmgr/) | `libwinmgrlib.so` (6 fns) | single-level window-handle accessors (one with side-effect) | byte-identical, 25 lines |
| [`cfxutil/`](cfxutil/) | `libConvertCfxNCK.so` (4 fns) | binary/UTF text + number string scanners | byte-identical, 43 lines |
| [`xmlhash/`](xmlhash/) | `libxmlreader.so` (3 fns) | Jenkins one-at-a-time hash + setters | byte-identical, 49 lines |
| [`bmx/`](bmx/) | `libQsBmxImageLibraryNoDbidLookup.so` (5 fns) | BMX/BMP image-header accessors + 24bpp size calc (multi-soname/Qt oracle) | byte-identical, 13 lines |

Each subdir is self-contained (`README.md` + `build_and_verify_*.sh`).

## Behavioral-equivalence instances — 13 libraries, 112 functions

Beyond the byte-identical set lie the classes that *cannot* be bit-identical
across the ISA boundary: computed FP / libm, and genuine C++ class methods
(`this` pointer, vtable-adjacent layout, x87 return registers). For these we
relax the bar from "same SHA-256" to **observable equivalence** — identical
outputs for identical inputs, exact for ints/bools and within a tight FP
tolerance for computed doubles — measured differentially against the real i386
`.so` under `qemu-i386`.

| Dir | Library | Class | Proof |
|---|---|---|---|
| [`bohrcyc_fp/`](bohrcyc_fp/) | `libEp90_Bohrcyc.so` (2 FP fns) | angle de-norm + sin/cos compare | 70957 vectors; return codes exact, doubles **0 ULP** |
| [`metaval/`](metaval/) | `libtncMetaValue.so` (15 C++ methods) | static unit-conv + `this`/pImpl accessors | 1283 vectors; ints exact, doubles 0 ULP |
| [`productid/`](productid/) | `libProductId.so` (13 C++ methods) | control-mark identity predicates | full-range drive; deterministic → same SHA-256 on output |
| [`dmathe/`](dmathe/) | `libEp90_Dm.so` (22 `dmathe_*` FP, full family) | 2D geometry (atan/sqrt/modf) | 12356 vectors; ints exact, doubles **0 ULP** |
| [`dkomp/`](dkomp/) | `libEp90_Dm.so` (23 `dkomp_*` nav, 5 families) | multi-level pointer chasers (linked list) | per-arch list, tag traversal → same SHA-256 |
| [`geolib/`](geolib/) | `libEp90_Geolib.so` (14 fns) | distances, angle classifiers, 4 element + 2 point predicates, 16-way sector classifier | 36174 vectors; ints exact, doubles **0 ULP** |
| [`geometri/`](geometri/) | `libEp90_Geometri.so` (3 fns) | coordinate-type flag classifiers (flat geotec) | 720 vectors → same SHA-256 |
| [`aequi/`](aequi/) | `libEp90_Aequi.so` (3 fns) | tolerance accessors + linked-list length counter | 115 vectors → same SHA-256 |
| [`anfahr/`](anfahr/) | `libEp90_Anfahr.so` (2 fns) | corner-angle + entry-radius clamp (disasm recovery) | 22669 vectors; doubles **0 ULP** |
| [`gewcyc/`](gewcyc/) | `libEp90_Gewcyc.so` (6 fns) | thread-cycle: geotec point/coord accessors (pointer-chased) + lift-angle switch | 488 vectors; doubles **0 ULP** |
| [`cyckkorr/`](cyckkorr/) | `libEp90_Cyckkorr.so` (2 fns) | quadrant point rotation + geotec attribute copy | 1340 vectors; doubles **0 ULP** |
| [`fraescyc/`](fraescyc/) | `libEp90_Fraescyc.so` (3 fns) | milling-cycle flat accessors (depth/lift/feed-type) | 8064 vectors; doubles **0 ULP** |
| [`drehcyc/`](drehcyc/) | `libEp90_Drehcyc.so` (1 fn) | allowance-active predicate (struct-by-value ABI) | 700 vectors → same SHA-256 |

### What "behavioral" buys, and its two enabling tricks

1. **Per-arch-native objects** crack the pointer-width wall. A C++ method that
   chases `this->fmt->flags` can't run off one shared byte buffer (i386 pointers
   are 4 bytes, ARM64 8). So the harness mirrors the class *field order* and
   builds the object **per-arch from identical logical inputs** — compiled for
   i386 it reproduces the original offsets, for ARM64 it lays out native
   pointers. Both the real method and the reimplementation read their own arch's
   correct layout. (`metaval`'s `TncMetaValue` pImpl accessors.)
2. **The `bool` low-byte contract.** i386 `bool`/predicate returns only define
   `al`; the upper `eax` bytes carry `CONCAT31`/`setb` leakage that is
   load-address-dependent garbage. Reading the harness prototype as `_Bool`
   compares exactly the ABI-defined byte — the honest contract for a function
   that returns a boolean. (Used across `metaval`, `productid`, `dmathe`.)

The oracle recipe (trim `DT_NEEDED`, soname/version stub, neuter ctors) is the
same as the byte-identical set; only the verification standard changes. The
`.text` here genuinely differs from the i386 original (x87 `fsincos` vs ARM
`bl sin`), so we claim *equivalent behavior*, not *identical bytes* — the right
bar for transcendental math and ABI-bound class methods.

### Generalised oracle recipe (the reusable part)

For a C++ lib whose leaf functions are libc-only but whose `.so` drags the HeROS
runtime: **trim** the heavy `DT_NEEDED` (patchelf, **all edits in one invocation** —
repeated calls corrupt larger `.so`s), **stub** the residual non-glibc imports with
an auto-generated `.so` (giving it the depended library's *soname* + a version
script when a `HEROSLIB_500.0`/`JHVOLUMELIB_500.0`/`Qt_5`/… `VERNEED` survives), and
**neuter** the C++ static ctors (`neuter_init.py` zeroes `DT_INIT/FINI[_ARRAY]`).
When the surviving `VERNEED` spans several sonames (Qt-linked libs), `bmx/gen_oracle.py`
emits one stub per file. The recompiled `.text` of every verified function is the
genuine proprietary code. Caveat learned the hard way: a candidate must be
**exported in `.dynsym`** to serve as the truth oracle — local symbols
(`hexbyte`, `FlModAccess`, …) aren't linkable.

## Honest scope

This works because these are **leaf functions**: pure computation (or libc-only
I/O), no C++ classes/vtables/exceptions, no shared global state. That is the
subset for which decompile→recompile yields *correct, equivalent native code*. It
does **not** generalise to the whole control (87 exes + 248 libs of coupled,
optimised C++) — see `docs/16-arm64-decompilation-and-translation.md §3`. The
value is twofold: it demonstrates the pipeline literally works and is verifiable
(now across fourteen libraries / 88 functions spanning integer accessors, FP table
math, a file parser, error/geometry classifiers, codecs, and a stateful stack),
and each `*_aarch64.so` is a genuine native-ARM64 piece usable when
standing up the userland-translation port (doc 16, option B), where native leaf
libs reduce the amount of i386 code that must be translated.
