# recomp/plcbin — libplcbin → native ARM64, proven equivalent

A third worked instance of the project goal, and the first on a **stateful file
parser** (not pure math): decompile a proprietary HEIDENHAIN i386 control library
and **recompile it to native Apple-Silicon ARM64**, proven behaviourally
identical to the original.

`libplcbin.so` reads a big-endian "BIN PLC binary module" (`.bin`) file: it
validates the 40-byte magic/version, reads the bincode offset/size, and walks a
token table into a flat `uint32` info struct (two variants: `ReadInfo` and
`ReadSPLCInfo`, the latter with derived-offset arithmetic).

## What's here

| File | What it is |
|---|---|
| `libplcbin_rebuilt.c` | Native re-implementation of all 5 exports (`PLCBin_Open/_Close/_ReadBinCode/_ReadInfo/_ReadSPLCInfo`) + the internal big-endian `getlong`/`getword` and `ReadBinCodeInfo`. Token tables and field-selector maps recovered from the `.so`. |
| `libplcbin_arm64.dylib` / `libplcbin_aarch64.so` | Recompiled lib, native macOS arm64 + aarch64-Linux. |
| `libplcbin_trim.so` | The **oracle**: a patchelf-trimmed copy of the *real* proprietary `.so` — genuine i386 machine code with the irrelevant `DT_NEEDED` entries removed so it loads standalone (see below). |
| `verify_plcbin.c` | One harness: builds a byte-identical crafted `.bin` on both platforms, parses it, dumps every observable output. |
| `build_and_verify_plcbin.sh` | Reproduces the build + differential proof end to end. |

## The proof

`build_and_verify_plcbin.sh` builds the same crafted `.bin` and runs the same
harness two ways:

1. **truth** — the real proprietary code (`libplcbin_trim.so`) under `qemu-i386`;
2. **rebuilt** — the native ARM64 recompile on the M2.

Result: `IDENTICAL`, same SHA-256. The dumped output exercises version
detection, big-endian field reads, **both** token-table mappings (incl. the
`BYTES >> 2` SPLC special case), the SPLC derived-offset arithmetic, bincode
streaming with cursor + reset, and all three error codes (missing file, bad
magic).

## The trimmed oracle (why it's still honest)

The real `libplcbin.so` lists ~24 `DT_NEEDED` libraries (libheros, libGMessage*,
glib, xml2, …) — the whole HeROS runtime, whose load-time constructors need the
kernel API and won't initialise under bare `qemu-i386`. But the **5 functions
under test reference only libc** plus the light `nckern::codecheck::Registration`
ctor. So the oracle is the real `.so` with the irrelevant NEEDED entries removed
by `patchelf` (soname set to `libplcbin_trim.so`). It is the original proprietary
machine code, unchanged in `.text` — only the loader's dependency list is
trimmed to what these functions actually use.

## Note on the handle

The opaque handle is created and consumed entirely inside the library, so its
internal layout need not match across architectures (i386 4-byte vs arm64 8-byte
`FILE*`). The rebuild uses a native handle struct. Only the **parsed outputs** —
which are `uint32` arrays — are observable, and those are reproduced exactly.

## Honest scope

Pure-leaf-at-the-function-level: these functions touch only libc. The packaging
drags the HeROS runtime, but the *code* recompiles and verifies cleanly. Does not
generalise to the coupled C++ product — see `docs/16 §3`.
