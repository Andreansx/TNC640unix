#!/usr/bin/env python3
"""
neuter_init.py <elf32.so> — disable a shared library's load-time constructors
and destructors by rewriting the DT_INIT / DT_FINI / DT_*_ARRAY[SZ] / DT_PREINIT*
tags in its PT_DYNAMIC to DT_DEBUG (an ignored tag).

Why this is sound here: the verified functions are PURE LEAVES (no dependence on
any global state the ctors would set up), so skipping the library's global
initialisation does not change their behaviour. The .text of those functions is
left byte-for-byte identical to the proprietary original — we only stop the
loader from running unrelated C++ static initialisers that would otherwise touch
the trimmed-away HeROS runtime and crash. ELF32 little-endian only.
"""
import sys, struct

DT_NULL = 0
DT_DEBUG = 21
# init/fini machinery tags to neutralise -> DT_DEBUG
KILL = {12, 13, 25, 26, 27, 28, 32, 33}

def main(path):
    with open(path, "rb") as f:
        b = bytearray(f.read())
    assert b[:4] == b"\x7fELF" and b[4] == 1, "not ELF32"
    e_phoff = struct.unpack_from("<I", b, 0x1c)[0]
    e_phentsize = struct.unpack_from("<H", b, 0x2a)[0]
    e_phnum = struct.unpack_from("<H", b, 0x2c)[0]
    PT_DYNAMIC = 2
    dyn_off = dyn_filesz = None
    for i in range(e_phnum):
        ph = e_phoff + i * e_phentsize
        p_type, p_offset = struct.unpack_from("<II", b, ph)
        if p_type == PT_DYNAMIC:
            p_filesz = struct.unpack_from("<I", b, ph + 16)[0]
            dyn_off, dyn_filesz = p_offset, p_filesz
            break
    assert dyn_off is not None, "no PT_DYNAMIC"
    n = dyn_filesz // 8
    killed = []
    for i in range(n):
        off = dyn_off + i * 8
        d_tag, d_val = struct.unpack_from("<iI", b, off)
        if d_tag == DT_NULL:
            break
        if d_tag in KILL:
            struct.pack_into("<iI", b, off, DT_DEBUG, 0)
            killed.append(d_tag)
    with open(path, "wb") as f:
        f.write(b)
    print(f"  neutered {len(killed)} init/fini dynamic entries: tags {killed}")

if __name__ == "__main__":
    main(sys.argv[1])
