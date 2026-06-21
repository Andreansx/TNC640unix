/*
 * libEp90_Wznorm_partial_rebuilt.c — native re-implementation of the
 * PURE-INTEGER leaf subset of the proprietary i386 libEp90_Wznorm.so
 * (EP90 tool-normalisation library), recovered via Ghidra
 * (work/re/out/libEp90_Wznorm.so.decomp.c) + canonical i386 disassembly.
 *
 * SCOPE: most of libEp90_Wznorm is geometry (winkelhalbierende / compare_winkel
 * via libm) or touches the Ep90 geometry libs — NOT leaves. This file reproduces
 * the pure tool-type codec + the integer tool-class classifiers, which reference
 * no proprietary externals (only libc strtol).
 *
 * The i386 originals use magic-number division (0x51eb851f /100, 0x66666667 /10);
 * C signed '/' and '%' are truncation-toward-zero on BOTH i386 and ARM64, so the
 * clean integer expressions below reproduce them bit-for-bit. cdecl, confirmed.
 *
 *   GeotecToIntWkzTyp(int)            packed "Geotec" tool code  -> decimal int
 *   IntToGeotecWkzTyp(int)            decimal int -> packed Geotec code
 *   AsciiToGeotecWkzTyp(const char*)  decimal string -> Geotec code (libc strtol)
 *   WerkzeugTyp(tool*, int*,int*,int*) decompose tool struct field +0xd8
 *   WZ_IsAussenWkz(tool*)             "is outer tool" predicate over the decode
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

/* field +0xd8 packs three nibble/byte components:
 *   hi  = field / 256                (main type, *param_2)
 *   mid = (field % 256) / 16         (sub type,  *param_3)
 *   lo  = field % 16                 (variant,   *param_4)
 * All toward-zero (matches i386 sar+correction lowering). */

int GeotecToIntWkzTyp(int code)
{
    int lo256 = code % 0x100;
    if (lo256 < 0) lo256 += 0xf;                 /* toward-zero /16 of a neg remainder */
    int q256 = code + 0xff;
    if (code >= 0) q256 = code;                  /* toward-zero /256 */
    return (q256 >> 8) * 100 + (lo256 >> 4) * 10 + code % 0x10;
}

int IntToGeotecWkzTyp(int v)
{
    return ((v % 100) / 10 + (v / 100) * 0x10) * 0x10 + v % 10;
}

int AsciiToGeotecWkzTyp(const char *s)
{
    long l = strtol(s, 0, 10);
    /* i386 does the range test on the 32-bit eax: (uint32)(eax-0xb) <= 0x3dc.
     * Mask to 32 bits so the wraparound matches a 32-bit long. (strtol overflow
     * past INT32 is out of scope — the real input domain is small tool codes.) */
    uint32_t e = (uint32_t)l;
    if ((uint32_t)(e - 0xbu) < 0x3ddu)
        return IntToGeotecWkzTyp((int)e);
    return 0;
}

int WerkzeugTyp(const void *tool, int *p_main, int *p_sub, int *p_var)
{
    int ret = 0;
    *p_var = 0; *p_sub = 0; *p_main = 0;
    if (tool != 0) {
        int field = *(const int *)((const char *)tool + 0xd8);
        int q = field + 0xff;
        if (field >= 0) q = field;
        *p_main = q >> 8;
        int r = field % 0x100;
        if (r < 0) r += 0xf;
        *p_sub = r >> 4;
        *p_var = field % 0x10;
        ret = *p_main;
        if (ret == 0) ret = *p_sub;
    }
    return ret;
}

bool WZ_IsAussenWkz(const void *tool)
{
    int main_t; unsigned sub; unsigned var;
    WerkzeugTyp(tool, &main_t, (int *)&sub, (int *)&var);
    switch (main_t) {
    case 1:
        if ((int)(sub + 10) > 0xc) { sub -= 4; goto joined; }
        if ((int)(sub + 10) > 10) return 2u < var - 5u;   /* LAB_c19 */
        break;                                            /* -> return true */
    case 2:
        if (sub == 1) return 2u < var - 5u;               /* LAB_c19 */
        sub -= 6;
    joined:
        if (sub < 2u) return (var & 0xfffffffdu) != 5u;
        return true;
    case 3:
        return 6u < sub - 1u || var != 6u;
    case 4:
        if ((int)(sub + 0x28) < 0x2d) { if ((int)(sub + 0x28) < 0x2a) return true; }
        else if (2u < sub - 7u) return true;
        return var != 6u;                                 /* LAB_c69 */
    case 5: {
        int i = (int)sub + 0x32;
        if (i < 0x35) { if (i < 0x33) return true; }
        else if (i != 0x38) return true;
        return var != 6u;                                 /* LAB_c69 */
    }
    case 6:
        if (sub - 1u < 7u) {
            if ((1u << ((sub - 1u) & 0x1f) & 0x6du) != 0u) return var != 6u;
            return true;
        }
        break;                                            /* -> return true */
    }
    return true;
}
