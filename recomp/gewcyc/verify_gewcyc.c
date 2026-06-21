/*
 * verify_gewcyc.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Builds a geotec (with a pointer-chased
 * direction sub-descriptor) per-arch and drives GCYC_Geostart/Geoziel; sweeps
 * GCYC_SimpelAbhebeWinkel over flags. Doubles full-precision (tolerant compare).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "gewcyc_layout.h"

extern void   GCYC_Geostart(const void*, double*, double*) __asm__("_Z13GCYC_GeostartP6geotecPdS1_");
extern void   GCYC_Geoziel(const void*, double*, double*)  __asm__("_Z12GCYC_GeozielP6geotecPdS1_");
extern double GCYC_SimpelAbhebeWinkel(unsigned long)       __asm__("_Z23GCYC_SimpelAbhebeWinkelm");
extern void   GCYC_SetInkSteigung(const void*, double*, unsigned short*, double) __asm__("_Z19GCYC_SetInkSteigungP6geotecPdPtd");
extern void   GCYC_LeseAltko(const void*, double*, double*, double*) __asm__("_Z14GCYC_LeseAltkoP6geotecPdS1_S1_");
extern void   GCYC_Hole_Spandaten(const void*, unsigned short, double*, double*, double*, double*, double*) __asm__("_Z19GCYC_Hole_SpandatenP4spantPdS1_S1_S1_S1_");

static int DI = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }

int main(void)
{
    /* GCYC_Geostart / GCYC_Geoziel over both flag states + several geometries */
    static const double pts[] = { -5.0, -1.0, 0.0, 2.5, 7.25 };
    for (int flag = 0; flag <= 2; flag++)
    for (unsigned a = 0; a < 5; a++)
    for (unsigned b = 0; b < 5; b++) {
        GcSub sub; memset(&sub, 0, sizeof sub); sub.flag = (unsigned char)flag;
        GcGeotec e; memset(&e, 0, sizeof e);
        e.sub = &sub;
        e.sx = pts[a];   e.sy = pts[b];
        e.ex = pts[b];   e.ey = pts[a];
        double ox = 0, oy = 0;
        GCYC_Geostart(&e, &ox, &oy); D(ox); D(oy);
        GCYC_Geoziel(&e, &ox, &oy);  D(ox); D(oy);
    }

    /* GCYC_SimpelAbhebeWinkel over the switch arms + misc flags */
    static const unsigned long fl[] = { 0, 1, 2, 4, 0x20, 0x21, 0x100, 0x100000, 0x200000, 0xffffffffUL };
    for (unsigned i = 0; i < sizeof(fl)/sizeof(fl[0]); i++)
        D(GCYC_SimpelAbhebeWinkel(fl[i]));

    /* GCYC_SetInkSteigung + GCYC_LeseAltko over the +0x10 sub-descriptor */
    static const double pitch[] = { -2.0, -0.001, 0.0, 0.001, 5.0 };
    static const unsigned short coordflags[] = { 0, 2, 4, 0x40, 6, 0x46, 0xffff };
    for (unsigned p = 0; p < 5; p++)
    for (unsigned cf = 0; cf < 7; cf++) {
        GcSub2 s2; memset(&s2, 0, sizeof s2);
        s2.d58 = 11.0; s2.d60 = pitch[p]; s2.d80 = 33.0; s2.flags = coordflags[cf];
        GcGeotec e; memset(&e, 0, sizeof e); e.sub2 = &s2;
        double op = 0; unsigned short fw = 0x0155;
        GCYC_SetInkSteigung(&e, &op, &fw, 0.01);
        D(op); D((double)fw);
        double a = 0, b = 0, c = 0;
        GCYC_LeseAltko(&e, &a, &b, &c);
        D(a); D(b); D(c);
    }
    /* LeseAltko null-descriptor path */
    {
        GcGeotec e; memset(&e, 0, sizeof e); e.sub2 = 0;
        double a = 9, b = 9, c = 9;
        GCYC_LeseAltko(&e, &a, &b, &c);
        D(a); D(b); D(c);
    }

    /* GCYC_Hole_Spandaten: walk a span list of length 6, read each index */
    {
        Span sp[6];
        for (int k = 0; k < 6; k++) {
            memset(&sp[k], 0, sizeof sp[k]);
            sp[k].next = (k + 1 < 6) ? &sp[k + 1] : 0;
            sp[k].d8 = k + 0.5; sp[k].d10 = k + 1.5; sp[k].d18 = k + 2.5;
            sp[k].d20 = k + 3.5; sp[k].d38 = k + 4.5;
        }
        for (int idx = 1; idx <= 6; idx++) {
            double a = 0, b = 0, c = 0, d = 0, e = 0;
            GCYC_Hole_Spandaten(&sp[0], (unsigned short)idx, &a, &b, &c, &d, &e);
            D(a); D(b); D(c); D(d); D(e);
        }
    }
    return 0;
}
