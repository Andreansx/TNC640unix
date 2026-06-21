/*
 * libConvertCfxNCK_partial_rebuilt.c — native re-implementation of the
 * PURE-INTEGER text/number leaf utilities exported by the proprietary i386
 * libConvertCfxNCK.so, recovered via i386 disassembly. These same helpers appear
 * in several control libs (libKinematicsDesign_sl, libConvertCfxNCK, …).
 *
 * All are call-free string scanners. cdecl. (HexAtol is excluded — it calls into
 * libc locale ctype, so it's not a pure leaf.)
 *   IsBinNumber  - is s an optional-'%'-prefixed string of only '0'/'1'
 *   BinAtol      - parse such a binary string to a 32-bit value (spaces ignored)
 *   IsUtf8       - does s start with the UTF-8 BOM (EF BB BF)
 *   utf16_strlen - length of a NUL-terminated uint16 string
 */
#include <stdint.h>

int IsBinNumber(const char *s)
{
    if (!s) return 0;
    const unsigned char *p = (const unsigned char *)s;
    if (*p == 0x25) p++;                 /* skip optional leading '%' */
    unsigned ch = *p;
    if (ch == 0) return 0;               /* empty after skip -> not a bin number */
    do {
        if ((unsigned char)(ch - 0x30) > 1u) return 0;   /* not '0'/'1' */
        p++;
        ch = *p;
    } while (ch != 0);
    return 1;
}

unsigned BinAtol(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (*p == 0x25) p++;                 /* skip optional leading '%' */
    uint32_t c = 0;                      /* 32-bit accumulator (i386 ecx wraps at 32) */
    if (*p == 0) return 0;
    for (;;) {
        unsigned ch = *p;
        if      (ch == 0x20) { /* space: skip, no shift */ }
        else if (ch == 0x30) { c = c * 2u; }
        else if (ch == 0x31) { c = c * 2u + 1u; }
        else break;                      /* any other char stops parsing */
        p++;
        if (*p == 0) break;
    }
    return c;
}

int IsUtf8(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] != 0xef) return 0;
    if (p[1] != 0xbb) return 0;
    return p[2] == 0xbf;
}

int utf16_strlen(const uint16_t *s)
{
    int i = 0;
    while (s[i]) i++;
    return i;
}
