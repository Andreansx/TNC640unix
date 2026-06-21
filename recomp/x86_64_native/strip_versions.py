#!/usr/bin/env python3
"""strip_versions.py <elf32.so> — neutralise symbol versioning so a trimmed
oracle loads standalone. Zeroes DT_VERSYM/DT_VERNEED/DT_VERNEEDNUM (->DT_DEBUG)
in PT_DYNAMIC. After --remove-needed drops a heavy lib, its leftover VERNEED
entry makes _dl_check_map_versions assert; without DT_VERSYM the loader treats
all syms as unversioned (glibc UND -> default version, proprietary UND -> our
weak stub, exported -> by name). ELF32 LE only."""
import sys, struct
DT_DEBUG = 21
KILL = {0x6ffffff0, 0x6ffffffe, 0x6fffffff}  # VERSYM, VERNEED, VERNEEDNUM
def main(path):
    b = bytearray(open(path, "rb").read())
    assert b[:4] == b"\x7fELF" and b[4] == 1, "not ELF32"
    e_phoff = struct.unpack_from("<I", b, 0x1c)[0]
    e_phentsize = struct.unpack_from("<H", b, 0x2a)[0]
    e_phnum = struct.unpack_from("<H", b, 0x2c)[0]
    dyn_off = dyn_sz = None
    for i in range(e_phnum):
        ph = e_phoff + i*e_phentsize
        p_type, p_offset = struct.unpack_from("<II", b, ph)
        if p_type == 2:  # PT_DYNAMIC
            dyn_off = p_offset; dyn_sz = struct.unpack_from("<I", b, ph+16)[0]; break
    assert dyn_off is not None
    killed = []
    for i in range(dyn_sz // 8):
        off = dyn_off + i*8
        d_tag = struct.unpack_from("<I", b, off)[0]
        if d_tag == 0: break
        if d_tag in KILL:
            struct.pack_into("<II", b, off, DT_DEBUG, 0); killed.append(hex(d_tag))
    open(path, "wb").write(b)
    print(f"  stripped versions: {killed}")
if __name__ == "__main__":
    main(sys.argv[1])
