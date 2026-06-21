/*
 * libEp90_Bohrcyc_partial_rebuilt.c — native re-implementation of the
 * PURE-INTEGER leaf subset of the proprietary i386 libEp90_Bohrcyc.so
 * (drilling-cycle library), recovered via Ghidra + i386 disassembly.
 *
 * SCOPE: libEp90_Bohrcyc is mostly NOT a leaf — most functions do FP geometry
 * (sin/sqrt via x87, divergent from ARM64 NEON / a different libm) or call out
 * to the Ep90 geometry libs. Only its pure-integer accessors are byte-identical-
 * verifiable; those are reproduced here. The FP functions (BCYC_WinkelGleich
 * uses sincos; BCYC_EntnormiereWinkel produces a computed +/-2*pi value) are
 * deliberately excluded — see README.
 *
 * Reproduced (exact, from disassembly):
 *   BCYC_Typisiere_Werkzeug(int, int*, unsigned*)  tool-type / sub-type decode
 *   BCYC_Angetr_Werkz(void*)                        driven-tool predicate
 */
#include <stdint.h>

/* *p2 = arg >> 8 (signed/arithmetic, `sar`); *p3 = (arg >> 4) & 0xf */
void BCYC_Typisiere_Werkzeug(int arg, int *p2, unsigned *p3)
{
    *p2 = arg >> 8;
    *p3 = (unsigned)((arg >> 4) & 0xf);
}

/* reads tool byte at +0xe0; returns eax|edx where setbe/sete touch only the low
 * byte, so the upper bytes of (t-1) "leak" into the result — reproduced exactly
 * for a bit-faithful 32-bit return:
 *   eax = ((t-1) & 0xffffff00) | (((t-1)&0xff) <= 1)
 *   edx = (t == 5)
 *   return eax | edx
 */
unsigned BCYC_Angetr_Werkz(const void *p)
{
    uint8_t  t   = ((const uint8_t *)p)[0xe0];
    uint32_t e   = (uint32_t)((int32_t)(uint32_t)t - 1);   /* t-1, 32-bit wrap */
    uint32_t eax = (e & 0xffffff00u) | (((e & 0xffu) <= 1u) ? 1u : 0u);
    uint32_t edx = (t == 5u) ? 1u : 0u;
    return eax | edx;
}
