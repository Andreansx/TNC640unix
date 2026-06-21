/*
 * libplibpp_bmx_partial_rebuilt.c — native re-implementation of the PURE-INTEGER
 * BMX/BMP image-header leaf accessors of the proprietary i386 libplibpp.so,
 * recovered via i386 disassembly. (Same .text also ships in libQsBmxImageLibrary;
 * libplibpp is used as the oracle because its dep tree is lighter than the Qt one.)
 *
 *   bmxBmxInfo(h)     -> *(uint32*)(h+0x10)
 *   bmxBmpInfo(h)     -> *(uint32*)(h+0)
 *   bmxBmxVersion(h)  -> *(uint8*)(h+8)   (zero-extended)
 *   bmxBmpData(h)     -> *(uint32*)(h+8)
 *   CheckSizeImage(h) -> validate dims; if unsized + 24bpp, compute the padded
 *                        image byte size, store it at h+0x14, return 1. cdecl.
 */
#include <stdint.h>

unsigned bmxBmxInfo(const void *h)    { return *(const uint32_t *)((const char *)h + 0x10); }
unsigned bmxBmpInfo(const void *h)    { return *(const uint32_t *)((const char *)h + 0x00); }
unsigned bmxBmxVersion(const void *h) { return *(const uint8_t  *)((const char *)h + 0x08); }
unsigned bmxBmpData(const void *h)    { return *(const uint32_t *)((const char *)h + 0x08); }

int CheckSizeImage(void *h)
{
    uint32_t *P = (uint32_t *)h;
    if (P[0x14 / 4] != 0) return 1;                              /* already sized */
    if (P[0x10 / 4] != 0) return 0;                              /* compressed/other -> 0 */
    if (*(uint16_t *)((char *)h + 0x0e) != 0x18) return 0;       /* not 24bpp -> 0 */
    int width  = (int)P[0x04 / 4];
    int height = (int)P[0x08 / 4];
    int c = width * 3;                                           /* 3 bytes/pixel */
    int e = (c - 1 >= 0) ? (c - 1) : (c + 2);                    /* cmovns */
    e >>= 2;                                                     /* /4 toward -inf (sar) */
    e += 1;
    e *= height;
    e <<= 2;                                                     /* dword-aligned rows * 4 */
    P[0x14 / 4] = (uint32_t)e;                                   /* store computed size */
    return 1;
}
