# recomp/xmlhash — `libxmlreader.so` hash → native ARM64, proven equivalent

Decompile → recompile → **byte-identical proof** for the hash + setter leaves of
the proprietary i386 `libxmlreader.so`.

## Verified functions (3)

| Function | Kind |
|---|---|
| `XmlKeyHashBinary(key, len)` | **Jenkins one-at-a-time hash** over `len` *signed* bytes (`movsx`): `h+=b; h+=h<<10; h^=h>>6;` per byte, then `h+=h<<3; h^=h>>11; h+=h<<15;`. Returns 0 for `len==0`. |
| `XmlHashSetKey` / `XmlHashSetValueAllocator` | single-level field stores into a hash handle |

The signed-byte (`movsx`) detail matters: bytes ≥ 0x80 are sign-extended, so the
harness includes a high-bit key (`\x80\x81\xff\x00\x7f`) and a 256-byte
incremental-length sweep alongside the fixed strings.

## Proof

`build_and_verify_xmlhash.sh`: same 49-line harness (`verify_xmlhash.c`) run as
real i386 `.so` under `qemu-i386` vs native ARM64 → **IDENTICAL** (same SHA-256).
Oracle technique as in [`../errplib`](../errplib/README.md) (stub soname
`libheros.so.1` for the `HEROSLIB_500.0` version requirement; libz/minizip trimmed).
