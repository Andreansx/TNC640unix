# recomp/bmx — BMX/BMP image-header accessors → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the BMX/BMP image-header leaf
accessors shipped (as identical `.text`) in several control libs. The oracle is
built from `libQsBmxImageLibraryNoDbidLookup.so` — the same code lives in
`libplibpp.so` / `libsvgreader.so`, but those are large `.so`s that patchelf
corrupts (section-past-EOF); the 75 KB Qt-plugin variant trims cleanly.

## Verified functions (5)

| Function | Kind |
|---|---|
| `bmxBmxInfo` / `bmxBmpInfo` / `bmxBmpData` | single `uint32` header-field reads (`+0x10` / `+0` / `+8`) |
| `bmxBmxVersion` | header version byte (`+8`, zero-extended) |
| `CheckSizeImage` | validate dims; if unsized + 24bpp, compute the dword-padded image byte size and store it at `+0x14`; return 1 |

`CheckSizeImage` reproduces the i386 row-padding math exactly (e.g. 17px×10 → row
51→52 bytes → 520; 100×50 → 15000; width 0 → 0).

## Proof

`build_and_verify_bmx.sh`: same 13-line harness (`verify_bmx.c`) run as real i386
`.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256).

## Oracle technique — MULTI-SONAME generalisation

This lib's surviving `VERNEED` spans **several sonames** — `Qt_5` is required from
`libQt5Svg/Gui/Core/Quick.so.5` and `Qt_5.15` from `libQt5Core.so.5`. Each VERNEED
entry independently requires *that file* to be loaded and define *that version*.
`gen_oracle.py` therefore emits **one stub `.so` per file**, each carrying that
soname and defining every version it is listed for (symbols duplicated across
files — fine for shared objects), plus a general stub for the unversioned imports.
We keep glibc + those sonames, trim the rest **in a single patchelf invocation**
(repeated calls corrupt the file), and neuter the C++ static ctors. This unblocks
Qt/codec-linked libraries in general, not just this one.
