/*
 * libEp90_Geolib_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the pure-FP geometry-math leaves
 * of the proprietary i386 libEp90_Geolib.so — distances, angle normalization,
 * and the sin/cos angle classifier that the GEOLIB_Is* predicates build on.
 * Proven OBSERVABLY EQUIVALENT to the real i386 .so under qemu-i386.
 *
 * Source of truth: work/re/out/libEp90_Geolib.so.decomp.c (Ghidra). Constants
 * from .rodata (base 0x10000): DAT_0001ff48 = 2*pi, DAT_0001ff70 = pi/2.
 */
#include <math.h>
#include <string.h>
#include <stdint.h>

#define GEO_TWO_PI 0x1.921fb54442d18p+2   /* DAT_0001ff48 */
#define GEO_PI_2   0x1.921fb54442d18p+0   /* DAT_0001ff70 */
#define GEO_EPS    0x1p-15                 /* DAT_0001ff3c = 2^-15 endpoint slack */

/* geo-element struct field offsets (read directly off the element pointer — no
 * indirection, so a flat byte buffer works identically on both arches):
 *   +0x24 startX  +0x2c startY  +0x5c type-flags(uint, 0x20=line 0x40=arc)
 *   +0x90 endX    +0x98 endY    +0xb0 radius  +0xb8 centerX  +0xc0 centerY */
static double GD(const void *p, int off){ double d; memcpy(&d, (const unsigned char*)p+off, 8); return d; }
static uint32_t GU(const void *p, int off){ uint32_t v; memcpy(&v, (const unsigned char*)p+off, 4); return v; }
#define G_SX(p) GD(p,0x24)
#define G_SY(p) GD(p,0x2c)
#define G_EX(p) GD(p,0x90)
#define G_EY(p) GD(p,0x98)
#define G_R(p)  GD(p,0xb0)
#define G_CX(p) GD(p,0xb8)
#define G_CY(p) GD(p,0xc0)
#define G_ANG(p) GD(p,0x3c)   /* line direction angle */
#define G_FLAGS(p) GU(p,0x5c)

/* abstand_pkt_pkt(x1,y1,x2,y2) — Euclidean distance between two points */
double abstand_pkt_pkt(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1, dy = y2 - y1;
    return sqrt(dx * dx + dy * dy);
}

/* abstand_pkt_gerade(px,py,qx,qy,angle) — signed perpendicular distance from
 * point (px,py) to the line through (qx,qy) at direction `angle`:
 *   cos(angle)*(py-qy) - (px-qx)*sin(angle) */
double abstand_pkt_gerade(double px, double py, double qx, double qy, double angle)
{
    double s = sin(angle), c = cos(angle);
    return c * (py - qy) - (px - qx) * s;
}

/* norm_winkel(angle, eps) — normalize into [0,2*pi), snap to 0 within eps of
 * either end. (Like dmathe_NormWinkel, but eps is a parameter here.) */
double norm_winkel(double angle, double eps)
{
    double ip;
    double r = modf(angle / GEO_TWO_PI, &ip) * GEO_TWO_PI;
    if (r < 0.0) r += GEO_TWO_PI;
    if (r < eps || (GEO_TWO_PI - r) < eps) r = 0.0;
    return r;
}

/* compare_sinus_winkel(s1,c1,s2,c2,eps) — classify two angles given by their
 * (sin,cos) pairs:
 *   1  : same angle           (|c1-c2|<eps && |s1-s2|<eps)
 *   5  : |c1+c2| >= eps       (not opposite-cosine-cancelling)
 *   3/5: else 3, or 5 if |s2+s1| >= eps
 * Returns a char (1/3/5) in the original. */
int compare_sinus_winkel(double s1, double c1, double s2, double c2, double eps)
{
    if (fabs(c1 - c2) < eps && fabs(s1 - s2) < eps)
        return 1;
    if (eps <= fabs(c1 + c2))
        return 5;
    return (eps <= fabs(s2 + s1)) * 2 + 3;
}

/* compare_winkel(a1, a2, tol) — classify the relationship of two angles:
 *   sincos both, classify via compare_sinus_winkel with eps = sin(tol); if the
 *   result is neither 1 nor 3, retry against a2+pi/2 and remap. Returns
 *   {1,3} (direct) or {2,4,5} (perpendicular retry). */
int compare_winkel(double a1, double a2, double tol)
{
    double s1 = sin(a1), c1 = cos(a1);
    double s2 = sin(a2), c2 = cos(a2);
    double e  = sin(tol);
    int r = compare_sinus_winkel(s1, c1, s2, c2, e);
    if (r != 1 && r != 3) {
        double a = a2 + GEO_PI_2;
        int r2 = compare_sinus_winkel(s1, c1, sin(a), cos(a), e);
        r = 2;
        if (r2 != 1) r = (r2 != 3) + 4;
    }
    return r;
}

/* oeffnungswinkel(a1, a2, dir, tol) — signed opening angle from a1 to a2, with
 * 2*pi unwrapping selected by the sign of dir; 0 if a1 and a2 compare equal. */
double oeffnungswinkel(double a1, double a2, double dir, double tol)
{
    if (compare_winkel(a1, a2, tol) == 1) return 0.0;
    double l2 = a1, l3 = a2;
    if (dir <= 0.0) {
        double l4 = a2 - tol;
        if (l2 < l4) { do { l2 += GEO_TWO_PI; } while (l2 < l4); }
    } else {
        double l4 = a1 - tol;
        if (l3 < l4) { do { l3 += GEO_TWO_PI; } while (l3 < l4); }
    }
    return l3 - l2;
}

/* GEOLIB_GetWinkelRichtung(angle, eps) — classify a direction into one of 16
 * sectors (octant centers + boundary bands), returning a power-of-2 code.
 * The angle is first normalized; then a left-to-right cascade compares it
 * against the octant boundaries π/4..7π/4 with ±eps bands. The return code is
 * the band where the cascade stops (2 = ≈0; 0x10000 = ≥7π/4+eps). The bands:
 *   code 2  : a ≈ 0 (|sin|<eps and |cos-1|<eps)
 *   4/8 .. 0x8000 : below/above each successive boundary
 *   0x10000 : past the last boundary. */
int GEOLIB_GetWinkelRichtung(double angle, double eps)
{
    static const double B[7] = {        /* octant boundaries, bit-exact */
        0x1.921fb54442d18p-1,           /* π/4   */
        0x1.921fb54442d18p+0,           /* π/2   */
        0x1.2d97c7f3321d2p+1,           /* 3π/4  */
        0x1.921fb54442d18p+1,           /* π     */
        0x1.f6a7a2955385ep+1,           /* 5π/4  */
        0x1.2d97c7f3321d2p+2,           /* 3π/2  */
        0x1.5fdbbe9bba775p+2,           /* 7π/4  */
    };
    double a = norm_winkel(angle, eps);
    double s = sin(a), c = cos(a);
    if (!(eps <= fabs(s) || eps <= fabs(c - 1.0)))   /* a ≈ 0 */
        return 2;
    int code = 4;
    for (int i = 0; i < 7; i++) {
        if (!(B[i] - eps <= a)) return code;   code <<= 1;
        if (!(B[i] + eps <= a)) return code;   code <<= 1;
    }
    return 0x10000;
}

/* GEOLIB_IsGeoringBereich(g1, g2, tol) — does g1's START coincide with g2's END
 * (and g1 != g2)? distance(g1.start, g2.end) < tol. */
int GEOLIB_IsGeoringBereich(const void *g1, const void *g2, double tol)
{
    if (g1 && g1 != g2)
        return abstand_pkt_pkt(G_SX(g1), G_SY(g1), G_EX(g2), G_EY(g2)) < tol;
    return 0;
}

/* wert_im_intervall(a, b, c, tol) — is value `a` within the closed interval
 * [min(b,c), max(b,c)] widened by tol? (orientation-agnostic) */
int wert_im_intervall(double a, double b, double c, double d)
{
    if (c <= b) {
        if (c < a + d) return a < d + b;
    } else if (b < a + d) {
        return a < c + d;
    }
    return 0;
}

/* wert_im_offenen_intervall(a, b, c, tol) — the open-interval variant. */
int wert_im_offenen_intervall(double a, double b, double c, double d)
{
    if (c <= b) {
        if (c + d <= a) return d + a <= b;
    } else if (b + d <= a) {
        return d + a <= c;
    }
    return 0;
}

/* flaeche_von_trapez(a, b, h) — trapezoid area = 0.5·(a+b)·h (DAT_ff40 = 0.5). */
double flaeche_von_trapez(double a, double b, double h)
{
    return 0.5 * (a + b) * h;
}

/* GEOLIB_IsStartpunkt(g, px, py, tol) — is (px,py) the START of element g?
 *   distance(g.start, point) < tol + 2^-15 */
int GEOLIB_IsStartpunkt(const void *g, double px, double py, double tol)
{
    if (!g) return 0;
    return abstand_pkt_pkt(G_SX(g), G_SY(g), px, py) < tol + GEO_EPS;
}

/* GEOLIB_IsZielpunkt(g, px, py, tol) — is (px,py) the END of element g? */
int GEOLIB_IsZielpunkt(const void *g, double px, double py, double tol)
{
    if (!g) return 0;
    return abstand_pkt_pkt(G_EX(g), G_EY(g), px, py) < tol + GEO_EPS;
}

/* GEOLIB_IsIdentisch(g1, g2, tol) — are two geometry elements the SAME element?
 * line: start1≈start2 and end1≈end2. arc (bit 0x40): also |r1-r2|<tol and
 * center1≈center2. Distances via abstand_pkt_pkt; <= tol. */
int GEOLIB_IsIdentisch(const void *g1, const void *g2, double tol)
{
    if (!g1 || !g2) return 0;
    uint32_t t = G_FLAGS(g1) & G_FLAGS(g2);
    if ((t & 0x20) == 0) {                 /* not-both-line -> arc path */
        if ((t & 0x40) &&
            fabs(G_R(g1) - G_R(g2)) < tol &&
            abstand_pkt_pkt(G_SX(g1), G_SY(g1), G_SX(g2), G_SY(g2)) <= tol &&
            abstand_pkt_pkt(G_EX(g1), G_EY(g1), G_EX(g2), G_EY(g2)) <= tol &&
            abstand_pkt_pkt(G_CX(g1), G_CY(g1), G_CX(g2), G_CY(g2)) <= tol)
            return 1;
        return 0;
    }
    if (abstand_pkt_pkt(G_SX(g1), G_SY(g1), G_SX(g2), G_SY(g2)) <= tol &&
        abstand_pkt_pkt(G_EX(g1), G_EY(g1), G_EX(g2), G_EY(g2)) <= tol)
        return 1;
    return 0;
}

/* GEOLIB_IsInvers(g1, g2, tol) — is g1 the REVERSE of g2?
 * line: start1≈end2 and end1≈start2. arc: also |r1+r2|<tol (opposite curvature)
 * and center1≈center2. */
int GEOLIB_IsInvers(const void *g1, const void *g2, double tol)
{
    if (!g1 || !g2) return 0;
    uint32_t t = G_FLAGS(g1) & G_FLAGS(g2);
    if ((t & 0x20) == 0) {                 /* arc path */
        if ((t & 0x40) &&
            fabs(G_R(g1) + G_R(g2)) < tol &&
            abstand_pkt_pkt(G_SX(g1), G_SY(g1), G_EX(g2), G_EY(g2)) <= tol &&
            abstand_pkt_pkt(G_EX(g1), G_EY(g1), G_SX(g2), G_SY(g2)) <= tol &&
            abstand_pkt_pkt(G_CX(g1), G_CY(g1), G_CX(g2), G_CY(g2)) <= tol)
            return 1;
        return 0;
    }
    if (abstand_pkt_pkt(G_SX(g1), G_SY(g1), G_EX(g2), G_EY(g2)) <= tol &&
        abstand_pkt_pkt(G_EX(g1), G_EY(g1), G_SX(g2), G_SY(g2)) <= tol)
        return 1;
    return 0;
}

/* GEOLIB_IsMathIdentisch(g1, g2, posTol, angTol) — same INFINITE element?
 * line: same direction (compare_winkel==1) and g1.start lies on g2's line
 * (|perp distance| <= posTol). arc: |r1-r2|<posTol and centers coincide. */
int GEOLIB_IsMathIdentisch(const void *g1, const void *g2, double posTol, double angTol)
{
    if (!g1 || !g2) return 0;
    uint32_t t = G_FLAGS(g1) & G_FLAGS(g2);
    if ((t & 0x20) == 0) {                 /* arc */
        if ((t & 0x40) && fabs(G_R(g1) - G_R(g2)) < posTol)
            return abstand_pkt_pkt(G_CX(g1), G_CY(g1), G_CX(g2), G_CY(g2)) <= posTol;
        return 0;
    }
    if (compare_winkel(G_ANG(g1), G_ANG(g2), angTol) == 1)
        return fabs(abstand_pkt_gerade(G_SX(g1), G_SY(g1), G_SX(g2), G_SY(g2), G_ANG(g2))) <= posTol;
    return 0;
}

/* GEOLIB_IsMathInvers(g1, g2, posTol, angTol) — same infinite element, OPPOSITE
 * direction (compare_winkel==3); arc uses |r1+r2|<posTol. */
int GEOLIB_IsMathInvers(const void *g1, const void *g2, double posTol, double angTol)
{
    if (!g1 || !g2) return 0;
    uint32_t t = G_FLAGS(g1) & G_FLAGS(g2);
    if ((t & 0x20) == 0) {                 /* arc */
        if ((t & 0x40) && fabs(G_R(g1) + G_R(g2)) < posTol)
            return abstand_pkt_pkt(G_CX(g1), G_CY(g1), G_CX(g2), G_CY(g2)) <= posTol;
        return 0;
    }
    if (compare_winkel(G_ANG(g1), G_ANG(g2), angTol) == 3)
        return fabs(abstand_pkt_gerade(G_SX(g1), G_SY(g1), G_SX(g2), G_SY(g2), G_ANG(g2))) <= posTol;
    return 0;
}
