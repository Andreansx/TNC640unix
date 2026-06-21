/*
 * libEp90_Gewcyc_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of leaf functions of the
 * proprietary i386 libEp90_Gewcyc.so (thread-cycle geometry):
 *   - GCYC_Geostart / GCYC_Geoziel : return the start/target point of a geotec,
 *     swapped according to a direction flag reached by a POINTER CHASE
 *     (*(g+0x14))+0x7b — so the geotec is built per-arch;
 *   - GCYC_SimpelAbhebeWinkel : lift-off angle from a flag (switch over consts).
 *
 * Source of truth: work/re/out/libEp90_Gewcyc.so.decomp.c (Ghidra).
 * Constants: DAT_24c40=π/2, DAT_24c50=π, DAT_24c90=3π/2.
 */
#include <math.h>
#include "gewcyc_layout.h"

#define G_PI_2   0x1.921fb54442d18p+0   /* DAT_24c40 */
#define G_PI     0x1.921fb54442d18p+1   /* DAT_24c50 */
#define G_3PI_2  0x1.2d97c7f3321d2p+2   /* DAT_24c90 */

/* GCYC_Geostart(g, *outX, *outY): if the direction flag (*(g+0x14))+0x7b == 1,
 * return the END point (g+0x90/0x98); else the START point (g+0x24/0x2c). */
void GCYC_Geostart(const void *g, double *ox, double *oy) __asm__("_Z13GCYC_GeostartP6geotecPdS1_");
void GCYC_Geostart(const void *g, double *ox, double *oy)
{
    const GcGeotec *e = (const GcGeotec *)g;
    if (e->sub->flag == 1) { *ox = e->ex; *oy = e->ey; }
    else                   { *ox = e->sx; *oy = e->sy; }
}

/* GCYC_Geoziel(g, *outX, *outY): the inverse — flag==1 returns START, else END. */
void GCYC_Geoziel(const void *g, double *ox, double *oy) __asm__("_Z12GCYC_GeozielP6geotecPdS1_");
void GCYC_Geoziel(const void *g, double *ox, double *oy)
{
    const GcGeotec *e = (const GcGeotec *)g;
    if (e->sub->flag == 1) { *ox = e->sx; *oy = e->sy; }
    else                   { *ox = e->ex; *oy = e->ey; }
}

/* GCYC_SetInkSteigung(g, *outPitch, *flags, eps): read the incremental pitch
 * from the +0x10 sub-descriptor (+0x60), output it, and set bit 9 (0x200) of
 * *flags unless |pitch| <= eps (then clear it). */
void GCYC_SetInkSteigung(const void *g, double *outPitch, unsigned short *flags, double eps)
    __asm__("_Z19GCYC_SetInkSteigungP6geotecPdPtd");
void GCYC_SetInkSteigung(const void *g, double *outPitch, unsigned short *flags, double eps)
{
    const GcGeotec *e = (const GcGeotec *)g;
    double pitch = e->sub2->d60;
    *outPitch = pitch;
    if (fabs(pitch) <= eps) *flags = *flags & 0xfdff;
    else                    *flags = *flags | 0x200;
}

/* GCYC_LeseAltko(g, *o1, *o2, *o3): read up to three alt-coordinates from the
 * +0x10 sub-descriptor, each gated by a flag bit (2/4/0x40) in flags@+0xa0;
 * outputs 0 where the bit is clear or the descriptor is null. */
void GCYC_LeseAltko(const void *g, double *o1, double *o2, double *o3)
    __asm__("_Z14GCYC_LeseAltkoP6geotecPdS1_S1_");
void GCYC_LeseAltko(const void *g, double *o1, double *o2, double *o3)
{
    const GcGeotec *e = (const GcGeotec *)g;
    *o1 = 0.0; *o2 = 0.0; *o3 = 0.0;
    const GcSub2 *s = e->sub2;
    if (s != 0) {
        unsigned short fl = s->flags;
        if (fl & 2)    *o1 = s->d58;
        if (fl & 4)    *o2 = s->d60;
        if (fl & 0x40) *o3 = s->d80;
    }
}

/* GCYC_Hole_Spandaten(list, index, *o1..*o5): walk the span list to the
 * (1-based) index-th node via next@+4, then read 5 doubles from it. */
void GCYC_Hole_Spandaten(const void *list, unsigned short index,
                         double *o1, double *o2, double *o3, double *o4, double *o5)
    __asm__("_Z19GCYC_Hole_SpandatenP4spantPdS1_S1_S1_S1_");
void GCYC_Hole_Spandaten(const void *list, unsigned short index,
                         double *o1, double *o2, double *o3, double *o4, double *o5)
{
    const Span *p = (const Span *)list;
    if (index > 1) {
        unsigned short i = 1;
        do { i++; p = p->next; } while (index != i);
    }
    *o1 = p->d18; *o2 = p->d20; *o3 = p->d8; *o4 = p->d10; *o5 = p->d38;
}

/* GCYC_SimpelAbhebeWinkel(flag): lift-off angle.
 *   0x20 -> 3π/2 ; 1 -> π ; 4 or 0x100000 -> 0 ; else -> π/2 */
double GCYC_SimpelAbhebeWinkel(unsigned long flag) __asm__("_Z23GCYC_SimpelAbhebeWinkelm");
double GCYC_SimpelAbhebeWinkel(unsigned long flag)
{
    if (flag == 0x20) return G_3PI_2;
    if (flag < 0x21) {
        if (flag == 1) return G_PI;
        if (flag != 4) return G_PI_2;
        return 0.0;                 /* flag == 4 */
    }
    if (flag != 0x100000) return G_PI_2;
    return 0.0;                     /* flag == 0x100000 */
}
