/*
 * verify_geolib.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Drives the pure-FP geometry leaves over
 * a deterministic grid. Doubles emitted full-precision (tolerant compare);
 * ints exact. longdouble (st0) returns read as double, as callers narrow them.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern double abstand_pkt_pkt(double, double, double, double);
extern double abstand_pkt_gerade(double, double, double, double, double);
extern double norm_winkel(double, double);
extern int    compare_sinus_winkel(double, double, double, double, double);
extern int    compare_winkel(double, double, double);
extern double oeffnungswinkel(double, double, double, double);
extern _Bool  GEOLIB_IsIdentisch(const void*, const void*, double);
extern _Bool  GEOLIB_IsInvers(const void*, const void*, double);
extern _Bool  GEOLIB_IsMathIdentisch(const void*, const void*, double, double);
extern _Bool  GEOLIB_IsMathInvers(const void*, const void*, double, double);
extern _Bool  GEOLIB_IsStartpunkt(const void*, double, double, double);
extern _Bool  GEOLIB_IsZielpunkt(const void*, double, double, double);
extern int    GEOLIB_GetWinkelRichtung(double, double);
extern _Bool  GEOLIB_IsGeoringBereich(const void*, const void*, double);
extern _Bool  wert_im_intervall(double, double, double, double);
extern _Bool  wert_im_offenen_intervall(double, double, double, double);
extern double flaeche_von_trapez(double, double, double);

/* build a flat geo element: flags + start/end/center points + radius */
static void set_geo(unsigned char *g, uint32_t flags, double sx, double sy,
                    double ex, double ey, double r, double cx, double cy)
{
    memset(g, 0, 0xC8);
    memcpy(g + 0x24, &sx, 8); memcpy(g + 0x2c, &sy, 8);
    memcpy(g + 0x90, &ex, 8); memcpy(g + 0x98, &ey, 8);
    memcpy(g + 0xb0, &r, 8);
    memcpy(g + 0xb8, &cx, 8); memcpy(g + 0xc0, &cy, 8);
    memcpy(g + 0x5c, &flags, 4);
}

static int DI = 0, II = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }
static void I(int v){ printf("I %d %d\n", II++, v); }

#include <math.h>

int main(void)
{
    /* distances */
    static const double c[] = { -9.3, -2.0, -0.25, 0.0, 0.7, 3.5, 88.0 };
    for (unsigned i = 0; i < sizeof(c)/sizeof(c[0]); i++)
    for (unsigned j = 0; j < sizeof(c)/sizeof(c[0]); j++)
    for (unsigned k = 0; k < sizeof(c)/sizeof(c[0]); k++)
    for (unsigned l = 0; l < sizeof(c)/sizeof(c[0]); l++)
        D(abstand_pkt_pkt(c[i], c[j], c[k], c[l]));

    /* point-to-line distance over points + directions */
    for (int i = -8; i <= 8; i++)
    for (int j = -8; j <= 8; j++)
    for (int a = 0; a < 8; a++)
        D(abstand_pkt_gerade(i * 0.7, j * 0.7, 1.0, -1.0, a * 0.7853981633974483));

    /* angle normalization over angles + several eps */
    static const double eps[] = { 1e-9, 1e-6, 1e-3, 1e-1 };
    for (int k = -120; k <= 120; k++)
        for (unsigned e = 0; e < sizeof(eps)/sizeof(eps[0]); e++)
            D(norm_winkel(k * 0.137, eps[e]));

    /* sin/cos angle classifier over angle pairs + eps */
    for (int i = -30; i <= 30; i++)
    for (int j = -30; j <= 30; j++) {
        double a = i * 0.21, b = j * 0.21;
        I(compare_sinus_winkel(sin(a), cos(a), sin(b), cos(b), 1e-3));
        I(compare_sinus_winkel(sin(a), cos(a), sin(b), cos(b), 1e-6));
    }

    /* angle classifier + opening angle over angle pairs, dirs, tolerances */
    static const double tl[] = { 1e-6, 1e-3, 1e-2 };
    for (int i = -24; i <= 24; i++)
    for (int j = -24; j <= 24; j++) {
        double a = i * 0.27, b = j * 0.27;
        for (unsigned t = 0; t < sizeof(tl)/sizeof(tl[0]); t++)
            I(compare_winkel(a, b, tl[t]));
        D(oeffnungswinkel(a, b, -1.0, 1e-3));
        D(oeffnungswinkel(a, b,  1.0, 1e-3));
    }

    /* GEOLIB_IsIdentisch / IsInvers over line + arc element pairs */
    static const double pts[] = { -2.0, 0.0, 1.5, 3.0 };
    static const uint32_t fl[] = { 0x20, 0x40, 0x60, 0x00 };  /* line, arc, both, none */
    unsigned char g1[0xC8], g2[0xC8];
    for (unsigned f = 0; f < 4; f++)
    for (unsigned a = 0; a < 4; a++)
    for (unsigned b = 0; b < 4; b++)
    for (unsigned c = 0; c < 4; c++)
    for (unsigned d = 0; d < 4; d++) {
        double sx1 = pts[a], sy1 = pts[b], ex1 = pts[c], ey1 = pts[d];
        /* g1: a directed element; g2 chosen to hit identical / reversed / off cases */
        set_geo(g1, fl[f], sx1, sy1, ex1, ey1, 2.5, 0.5, -0.5);
        /* identical */
        set_geo(g2, fl[f], sx1, sy1, ex1, ey1, 2.5, 0.5, -0.5);
        I(GEOLIB_IsIdentisch(g1, g2, 1e-6)); I(GEOLIB_IsInvers(g1, g2, 1e-6));
        /* reversed (start<->end), opposite radius */
        set_geo(g2, fl[f], ex1, ey1, sx1, sy1, -2.5, 0.5, -0.5);
        I(GEOLIB_IsIdentisch(g1, g2, 1e-6)); I(GEOLIB_IsInvers(g1, g2, 1e-6));
        /* slightly off (within a looser tol) */
        set_geo(g2, fl[f], sx1 + 0.4, sy1, ex1, ey1 + 0.4, 2.5, 0.5, -0.5);
        I(GEOLIB_IsIdentisch(g1, g2, 1.0)); I(GEOLIB_IsIdentisch(g1, g2, 1e-6));
    }
    I(GEOLIB_IsIdentisch(0, g2, 1e-6));   /* null guards */
    I(GEOLIB_IsInvers(g1, 0, 1e-6));

    /* GEOLIB_IsMathIdentisch / IsMathInvers: lines with directions + arcs */
    static const double ang[] = { 0.0, 0.7853981633974483, 1.5707963267948966,
                                  3.141592653589793, 4.71238898038469 };
    for (unsigned f = 0; f < 2; f++)            /* line (0x20) then arc (0x40) */
    for (unsigned a = 0; a < 5; a++)            /* g1 direction              */
    for (unsigned b = 0; b < 5; b++)            /* g2 direction              */
    for (int dx = -1; dx <= 1; dx++)            /* g2 start offset           */
    for (int dr = -1; dr <= 1; dr++) {          /* g2 radius delta           */
        uint32_t fl1 = (f == 0) ? 0x20u : 0x40u;
        double a1 = ang[a], a2 = ang[b];
        set_geo(g1, fl1, 1.0, 2.0, 4.0, 6.0, 3.0, 0.5, -0.5);
        memcpy(g1 + 0x3c, &a1, 8);
        set_geo(g2, fl1, 1.0 + dx * 0.5, 2.0, 4.0, 6.0, 3.0 + dr * 0.5, 0.5, -0.5);
        memcpy(g2 + 0x3c, &a2, 8);
        I(GEOLIB_IsMathIdentisch(g1, g2, 1e-3, 1e-3));
        I(GEOLIB_IsMathInvers(g1, g2, 1e-3, 1e-3));
        I(GEOLIB_IsMathIdentisch(g1, g2, 2.0, 1e-2));
        I(GEOLIB_IsMathInvers(g1, g2, 2.0, 1e-2));
    }

    /* GEOLIB_IsStartpunkt / IsZielpunkt: probe points near/at the endpoints */
    set_geo(g1, 0x20, 1.0, 2.0, 4.0, 6.0, 0.0, 0.0, 0.0);
    for (int i = -6; i <= 6; i++)
    for (int j = -6; j <= 6; j++) {
        double px = 1.0 + i * 0.5, py = 2.0 + j * 0.5;   /* around the start */
        I(GEOLIB_IsStartpunkt(g1, px, py, 0.3));
        I(GEOLIB_IsStartpunkt(g1, px, py, 1e-6));
        double qx = 4.0 + i * 0.5, qy = 6.0 + j * 0.5;   /* around the end   */
        I(GEOLIB_IsZielpunkt(g1, qx, qy, 0.3));
        I(GEOLIB_IsZielpunkt(g1, qx, qy, 1e-6));
    }
    I(GEOLIB_IsStartpunkt(0, 0.0, 0.0, 0.1));            /* null guard */

    /* GEOLIB_GetWinkelRichtung: dense angle sweep (incl. exact octant hits) + eps */
    static const double we[] = { 1e-6, 1e-3, 1e-2 };
    for (int k = -400; k <= 400; k++)
        for (unsigned e = 0; e < sizeof(we)/sizeof(we[0]); e++)
            I(GEOLIB_GetWinkelRichtung(k * 0.0157079632679, we[e]));  /* ~step π/200 */
    for (int oct = 0; oct < 8; oct++)                    /* land exactly on octants */
        for (unsigned e = 0; e < sizeof(we)/sizeof(we[0]); e++)
            I(GEOLIB_GetWinkelRichtung(oct * 0.7853981633974483, we[e]));

    /* GEOLIB_IsGeoringBereich: g1.start vs g2.end coincidence */
    for (int i = -5; i <= 5; i++)
    for (int j = -5; j <= 5; j++) {
        set_geo(g1, 0x20, 1.0, 2.0, 9.0, 9.0, 0.0, 0.0, 0.0);
        set_geo(g2, 0x20, 7.0, 7.0, 1.0 + i * 0.5, 2.0 + j * 0.5, 0.0, 0.0, 0.0);
        I(GEOLIB_IsGeoringBereich(g1, g2, 0.4));
        I(GEOLIB_IsGeoringBereich(g1, g2, 1e-6));
    }
    I(GEOLIB_IsGeoringBereich(g1, g1, 0.4));   /* same-element guard -> false */
    I(GEOLIB_IsGeoringBereich(0, g2, 0.4));    /* null guard */

    /* wert_im_intervall / wert_im_offenen_intervall / flaeche_von_trapez */
    static const double iv[] = { -4.0, -1.0, -0.001, 0.0, 0.001, 1.0, 4.0 };
    for (unsigned a = 0; a < 7; a++)
    for (unsigned b = 0; b < 7; b++)
    for (unsigned c = 0; c < 7; c++)
    for (unsigned d = 0; d < 4; d++) {
        double tol = d * 0.5;
        I(wert_im_intervall(iv[a], iv[b], iv[c], tol));
        I(wert_im_offenen_intervall(iv[a], iv[b], iv[c], tol));
        D(flaeche_von_trapez(iv[a], iv[b], iv[c]));
    }
    return 0;
}
