/*
 * libfile_partial_rebuilt.c — native re-implementation of the PURE-INTEGER
 * EXPORTED leaf subset of the proprietary i386 libfile.so (HeROS file layer),
 * recovered via Ghidra (work/re/out/libfile.so.decomp.c) + i386 disassembly.
 *
 * SCOPE: most of libfile is the file/record/cache machinery (HeROS queues,
 * message bus) — NOT leaves. This file reproduces the small self-contained
 * predicates/bit test that are EXPORTED in .dynsym. (FlModAccess is also a pure
 * leaf but a local symbol, so it is out of the dynamically-linkable set.) cdecl.
 */
#include <stdint.h>

/* (1 << (bit&7)) & (signed char)byte[ floor(bit/8 toward zero) ]
 * i386: cmovns picks bit vs bit+7 before `sar 3` to divide toward zero;
 *       movsx sign-extends the byte (masked off above bit 7 anyway). */
unsigned BitFieldTst(const void *p, int bit)
{
    int idx = (bit >= 0) ? bit : bit + 7;
    idx >>= 3;
    int b = *(const signed char *)((const char *)p + idx);
    return (unsigned)((1 << (bit & 7)) & b);
}

/* NC file-type tag: t in {4,5} (t<6 && t>3) or {0x26,0x27} ((uint)(t-0x26)<2). */
int IsNcFile(int t)
{
    if (t < 6) return t > 3;
    return (unsigned)(t - 0x26) < 2u;
}

/* ASCII file-type tag: membership test over a fixed code set (i386 jumptable). */
int IsAscFile(int t)
{
    switch (t) {
    case 9: case 10: case 0xc: case 0x11: case 0x12: case 0x13:
    case 0x14: case 0x15: case 0x23: case 0x28: case 0x2a: case 0x2b:
        return 1;
    default:
        return 0;
    }
}

/* server-list header: count at +4 */
unsigned FlServerListSize(const void *p)
{
    return *(const uint32_t *)((const char *)p + 4);
}

/* xor eax,eax ; ret */
unsigned read_mminch(void)
{
    return 0;
}
