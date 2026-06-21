/*
 * verify_cyckkorr.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Drives renormiere_punkt over points x
 * quadrants x flags, and ckk_uebertrage_attribute over a crafted geotec pair.
 * Doubles full-precision (tolerant compare); integer byte reads exact.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern void renormiere_punkt(double*, double*, int, int) __asm__("_Z16renormiere_punktPdS_6hsr_ati");
extern void ckk_uebertrage_attribute(void*, void*)        __asm__("_Z24ckk_uebertrage_attributeP6geotecS0_");

static int DI = 0, II = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }
static void I(int v){ printf("I %d %d\n", II++, v); }

int main(void)
{
    /* renormiere_punkt over points, quadrants 0..5 (incl. out-of-range), flags */
    static const double pts[] = { -7.5, -1.0, 0.0, 0.3, 4.25, 100.0 };
    for (int q = 0; q <= 5; q++)
    for (int fl = 0; fl <= 2; fl++)
    for (unsigned a = 0; a < 6; a++)
    for (unsigned b = 0; b < 6; b++) {
        double x = pts[a], y = pts[b];
        renormiere_punkt(&x, &y, q, fl);
        D(x); D(y);
    }

    /* ckk_uebertrage_attribute: fill src with distinct bytes, zero dst, copy,
     * then read back dst at every copied field. */
    unsigned char src[0x98], dst[0x98];
    for (int t = 0; t < 4; t++) {
        for (int i = 0; i < 0x98; i++) src[i] = (unsigned char)(i * 7 + t * 13 + 1);
        memset(dst, 0, sizeof dst);
        ckk_uebertrage_attribute(src, dst);
        static const int offs[] = { 0x64, 0x68, 0x6c, 0x6d, 0x70, 0x74, 0x78, 0x7c, 0x84, 0x88, 0x8c };
        for (unsigned k = 0; k < sizeof(offs)/sizeof(offs[0]); k++) {
            uint32_t v; memcpy(&v, dst + offs[k], 4);
            I((int)v);
        }
    }
    return 0;
}
