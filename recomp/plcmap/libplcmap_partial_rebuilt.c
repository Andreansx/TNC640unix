/*
 * libplcmap_partial_rebuilt.c — native re-implementation of the PURE-INTEGER
 * leaf subset of the proprietary i386 libplcmap.so (PLC I/O symbol map),
 * recovered via Ghidra (work/re/out/libplcmap.so.decomp.c) + i386 disassembly.
 *
 * SCOPE: most of libplcmap walks the symbol map / touches globals — NOT leaves.
 * This file reproduces the self-contained endian/compare/decimal-width helpers
 * that are EXPORTED in the .dynsym (linkable against the real .so). cdecl.
 * (hexbyte / pmap_getsymbollen / pmap_setmode are also pure leaves but are local
 *  symbols — not dynamically linkable — so they are out of the verifiable set.)
 */
#include <stdint.h>

/* bswap32 */
unsigned Swap_d(unsigned v)
{
    return v >> 24 | (v & 0xff0000u) >> 8 | (v & 0xff00u) << 8 | v << 24;
}

/* movzx eax,word ; rol ax,8  -> low 16 swapped, upper 16 bits zero */
unsigned Swap_w(uint16_t v)
{
    return (unsigned)(uint16_t)((uint16_t)(v << 8) | (uint16_t)(v >> 8));
}

/* unsigned 64-bit compare, (a_hi:a_lo) vs (b_hi:b_lo); +1 / -1 / 0 */
int UQuadCompare(unsigned a_hi, unsigned a_lo, unsigned b_hi, unsigned b_lo)
{
    if (a_hi > b_hi) return 1;
    if (a_hi < b_hi) return -1;
    if (a_lo > b_lo) return 1;
    if (a_lo < b_lo) return -1;
    return 0;
}

/* number of characters to print a signed int as decimal (incl. '-').
 * Reproduces the i386 signed compares exactly — including the INT_MIN quirk
 * (its abs stays 0x80000000, treated as <=9 by the signed test, so the loop is
 * skipped and the result is 2, not 11). */
int NumberOfCharacters(int param)
{
    int32_t sign = param >> 31;                                   /* sar 0x1f */
    uint32_t a = (uint32_t)(sign ^ param) - (uint32_t)sign;       /* abs (two's comp) */
    int cnt = 1;
    if ((int32_t)a > 9) {                                         /* cmp edx,9 ; jle */
        uint32_t prev;
        do {
            prev = a;
            cnt += 1;
            a = a / 10u;                                          /* magic /10 (unsigned) */
        } while ((int32_t)prev > 99);                             /* cmp ecx,0x63 ; jg */
    }
    return (cnt + 1) - ((uint32_t)param < 0x80000000u ? 1 : 0);   /* sbb sign adjust */
}
