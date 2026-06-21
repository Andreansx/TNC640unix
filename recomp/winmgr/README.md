# recomp/winmgr — `libwinmgrlib.so` accessors → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the exported single-level
accessors of the proprietary i386 `libwinmgrlib.so` (HeROS window manager).

## Verified functions (6 exported)

| Function | Kind |
|---|---|
| `CheckWindow` | predicate + side-effect: if `*win==0 && *(win+8)==*out` then `*(out+4)=*(win+0xc)`; returns 0 |
| `WmGetMessageCount` | `p ? *(p+0x4c) : 0` |
| `WmMustConfirmEvent` | `*(p+0x38)` |
| `AllocWindow` | `++*(p+0x44)` (counter bump, returns new value) |
| `WmGetLastError` | read-and-clear `*(p+0x3c)` |
| `FreeWindow` | noop |

All operate on single fields of a caller-provided window handle, so the harness
drives them over crafted flat buffers (treating the fields as opaque uint32) and
observes the returns plus the buffer mutations.

## Proof

`build_and_verify_winmgr.sh`: same 25-line harness (`verify_winmgr.c`) run as real
i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256). The
real `.so` pulls in libX11/libXext + the HeROS bus; the oracle trims those and
satisfies the residual `HEROSLIB_500.0` `VERNEED` with a stub of soname
`libheros.so.1` (recipe as in [`../errplib`](../errplib/README.md)).
