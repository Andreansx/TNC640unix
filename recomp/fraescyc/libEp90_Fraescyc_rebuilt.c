/*
 * libEp90_Fraescyc_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the FCYC_* milling-cycle
 * accessor leaves of the proprietary i386 libEp90_Fraescyc.so. Each reads a
 * tec_cycfraes_rt struct by FLAT field offset (no pointer indirection) and
 * computes a layer count / depth / lift length / feed type. Proven OBSERVABLY
 * EQUIVALENT to the real i386 .so under qemu-i386.
 *
 * Source of truth: work/re/out/libEp90_Fraescyc.so.decomp.c (Ghidra).
 * Constants: DAT_3033c = 0.12500002956949174, DAT_30360 = 5e37.
 */
#include <math.h>
#include <string.h>
#include <stdint.h>

static double      GD(const void *p, int o){ double d; memcpy(&d, (const char*)p+o, 8); return d; }
static int32_t     GI(const void *p, int o){ int32_t v; memcpy(&v, (const char*)p+o, 4); return v; }
static signed char GC(const void *p, int o){ return *((const signed char*)p + o); }

#define FC_BIG   5e37                   /* DAT_30360 */

/* NOTE: FCYC_AnzahlSchichten is deliberately excluded — it converts an x87
 * 80-bit intermediate via `fisttpl` (truncation), and its exact value depends
 * on the AT&T-reversed `fdivp`/`fsubp` operand order plus 80-bit rounding at an
 * integer boundary; neither double nor 128-bit long double on ARM reproduces it
 * bit-exactly, so a clean differential proof isn't available. */

/* FCYC_FraesTiefe(p) — milling depth (+0x74) if the element is active, else 0. */
double FCYC_FraesTiefe(const void *p)
{
    if (p != 0 && GI(p, 0x10) != 0) return GD(p, 0x74);
    return 0.0;
}

/* FCYC_AbhebeLaenge(p, v) — lift length |p[0x148]-v| if p[0x148] is finite-ish
 * and the length meets the +0x94 threshold; else 0. */
double FCYC_AbhebeLaenge(const void *p, double v)
{
    double a = GD(p, 0x148);
    if (fabs(a) < FC_BIG) {
        double l = fabs(a - v);
        if (GD(p, 0x94) <= l) return l;
    }
    return 0.0;
}

/* FCYC_VorschubArt(p, a, b) — feed-motion type (0 or 2). */
signed char FCYC_VorschubArt(const void *p, int a, int b)
{
    if (GI(p, 0x16c) == 0)
        return (signed char)((GC(p, 0x192) == 0) * 2);
    if (GD(p, 0x94) <= fabs(GD(p, 0x170))) return 0;
    if (GC(p, 0x192) != 0 && (b < 2 || a == 1)) return 0;
    return 2;
}
