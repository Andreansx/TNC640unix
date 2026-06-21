/*
 * libEp90_Dm_dmathe_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the `dmathe_*` 2D-geometry math
 * leaves of the proprietary i386 libEp90_Dm.so. These are the canonical
 * "computed FP / libm" class that was excluded from the byte-identical bar
 * (x87 80-bit intermediates vs ARM 64-bit, atan/sqrt/modf). We prove OBSERVABLE
 * EQUIVALENCE: same outputs for same inputs, within a tight FP tolerance,
 * differentially against the genuine i386 .so under qemu-i386.
 *
 * Source of truth: work/re/out/libEp90_Dm.so.decomp.c (Ghidra). All FP
 * constants lifted verbatim from .rodata (base 0x10000):
 *   c8e8=pi/2  c8f8=1e-3  c900=2pi  c908=pi  c920=1.0000000084983168e-9
 *   c928=3pi/2 c930=-1e-3 c938=1e-8 ; floats c940=-0.5 c944=1.0 c948=0.5 c94c=0.25
 */
#include <math.h>

#define PI_2      0x1.921fb54442d18p+0   /* c8e8 */
#define PI        0x1.921fb54442d18p+1   /* c908 */
#define TWO_PI    0x1.921fb54442d18p+2   /* c900 */
#define THREE_PI2 0x1.2d97c7f3321d2p+2   /* c928 */
#define SNAP_EPS  0x1.12e0bea99e73fp-30  /* c920, ~1e-9 */
#define EPS3      0.001                  /* c8f8 (nearest double == literal)  */
#define NEPS3     (-0.001)               /* c930 */
#define EPS8      1e-8                   /* c938 */

/* dmathe_TauscheD(double*, double*) — swap two 64-bit values */
void dmathe_TauscheD(double *a, double *b) { double t = *a; *a = *b; *b = t; }

/* dmathe_RightPerpVect(x, y, out) — out = {-x, y} */
void dmathe_RightPerpVect(double x, double y, double *out) { out[1] = y; out[0] = -x; }

/* dmathe_LeftPerpVect(x, y, out) — out = {x, -y} */
void dmathe_LeftPerpVect(double x, double y, double *out) { out[0] = x; out[1] = -y; }

/* dmathe_PunktDrehen(p, m2, m3, m4, m5) — rotate/translate point in place */
void dmathe_PunktDrehen(double *p, double m2, double m3, double m4, double m5)
{
    double ox = p[0];
    p[0] = m2 * (ox - m5) + m3 * (p[1] - m4) + m5;
    p[1] = ((p[1] - m4) * m2 + m4) - (ox - m5) * m3;
}

/* dmathe_roundst(double x, int n) — round(x*n, half away from 0) / n */
double dmathe_roundst(double x, int n) __asm__("_Z14dmathe_roundstdi");
double dmathe_roundst(double x, int n)
{
    double ip;
    double arg = (x <= 0.0) ? x * (double)n - 0.5 : x * (double)n + 0.5;
    modf(arg, &ip);
    return ip / (double)n;
}

/* dmathe_NormWinkel(double a) — normalize angle into [0, 2pi), snap near 0/2pi */
double dmathe_NormWinkel(double a)
{
    double ip;
    double r = modf(a / TWO_PI, &ip) * TWO_PI;
    if (r < 0.0) r = TWO_PI + r;
    if (r < SNAP_EPS || (TWO_PI - r) < SNAP_EPS) r = 0.0;
    return r;
}

/* dmathe_Wirein(double a) — wrap angle into (-pi, pi] (as fraction*2pi) */
double dmathe_Wirein(double a)
{
    double ip;
    double frac = modf(a / TWO_PI, &ip);
    if (frac <= -0.5) frac += 1.0;
    if (0.5 < frac)   frac -= 1.0;
    return frac * TWO_PI;
}

/* dmathe_VectorWinkel(double x, double y) — direction angle via atan */
double dmathe_VectorWinkel(double x, double y)
{
    double ax = (x < 0.0) ? -x : x;
    if (ax < EPS3) {
        if (y < EPS3) return PI;
        return 0.0;
    }
    double t = atan(y / x);
    if (EPS3 <= x) return PI_2 - t;
    return THREE_PI2 - t;
}

/* dmathe_Distance(x1,y1,x2,y2) — euclidean distance, 0 when within eps box */
double dmathe_Distance(double x1, double y1, double x2, double y2)
{
    double ady = (y1 >= y2) ? (y1 - y2) : (y2 - y1);
    double adx = (x1 >= x2) ? (x1 - x2) : (x2 - x1);
    if (ady < EPS3 && adx < EPS3) return 0.0;
    double dx = x1 - x2, dy = y1 - y2;
    return sqrt(dx * dx + dy * dy);
}

/* dmathe_Turn180Degree(v, a) — negate v, advance angle by pi mod 2pi */
double dmathe_Turn180Degree(double *v, double a)
{
    v[1] = -v[1];
    v[0] = -v[0];
    double r = a + PI;
    if (TWO_PI < r) r -= TWO_PI;
    return r;
}

/* dmathe_CalcOeffWinkel(double a, double b, char flag) — opening angle */
double dmathe_CalcOeffWinkel(double a, double b, int flag)
{
    if (flag == 1) {
        if (EPS3 <= a - b) return (b + TWO_PI) - a;
    } else {
        if (b - a < EPS3) return b - a;
        a = a + TWO_PI;
    }
    return b - a;
}

/* dmathe_QuadGl(a,b,c, *x1, *x2) — solve a*x^2+b*x+c=0; returns root count */
int dmathe_QuadGl(double a, double b, double c, double *x1, double *x2)
{
    double aa = (a < 0.0) ? -a : a;
    int ret = 0;
    if (EPS3 <= aa) {
        b = b / a;
        double disc = b * b * 0.25 - c / a;
        if (0.0 <= disc) {
            if (EPS8 <= disc) {
                double s = sqrt(disc);
                *x1 = s + (-b * 0.5);
                *x2 = (-b * 0.5) - s;
                return 2;
            }
            *x1 = -(b * 0.5);
            ret = 1;
        }
    }
    return ret;
}

/* dmathe_InIntervall(a,b,c) — is a within [b,c] (orientation-agnostic, eps) */
int dmathe_InIntervall(double a, double b, double c)
{
    if (c - b < EPS3) {
        if (NEPS3 < a - c) return NEPS3 < b - a;
    } else if (NEPS3 < a - b) {
        return NEPS3 < c - a;
    }
    return 0;
}

/* dmathe_wlinks(a,b) — angle a is to the left of b */
int dmathe_wlinks(double a, double b)
{
    if (PI - b < EPS3) {
        if ((b - PI) - a < EPS3) return EPS3 <= a - b;
    } else if (a - b < EPS3) {
        return EPS3 <= (b + PI) - a;
    }
    return 1;
}

/* dmathe_wrechts(a,b) — angle a is to the right of b */
int dmathe_wrechts(double a, double b)
{
    if (PI - b < EPS3) {
        if (EPS3 <= a - (b - PI)) return EPS3 <= b - a;
    } else if (EPS3 <= b - a) {
        return EPS3 <= a - (b + PI);
    }
    return 0;
}

/* dmathe_antiparallel(a,b) — direction a opposite to b (mod 2pi, eps) */
int dmathe_antiparallel(double a, double b)
{
    double n = dmathe_NormWinkel(b + PI);
    double d = a - n;
    double ad = (d < 0.0) ? -d : d;
    if (ad < EPS3) return 1;
    if (TWO_PI <= ad) return (ad - TWO_PI) < EPS3;
    return (TWO_PI - ad) < EPS3;
}

/* dmathe_Winkelstrecke(x1,y1,x2,y2) — direction angle of the segment via atan */
double dmathe_Winkelstrecke(double x1, double y1, double x2, double y2)
{
    double dx = x2 - x1;
    double adx = (dx < 0.0) ? -dx : dx;
    if (adx < EPS3) {
        if (y2 - y1 < EPS3) return PI;
        return 0.0;
    }
    double t = atan((y2 - y1) / dx);
    if (EPS3 <= dx) return PI_2 - t;
    return THREE_PI2 - t;
}

/* dmathe_SpGreater0(a,b,c,d,e,f) — sign of a dot product (degenerate fallback) */
int dmathe_SpGreater0(double a, double b, double c, double d, double e, double f)
{
    double v = c * a + d * b;
    double av = (v < 0.0) ? -v : v;
    if (av < EPS3) v = a * e + b * f;
    return EPS3 < v;
}

/* dmathe_RadAufBogen(start, end, dir, p) — is angle p on the arc [start,end] */
int dmathe_RadAufBogen(double start, double end, int dir, double p)
{
    if (dir == -1) { double t = start; start = end; end = t; }
    if (end < start) end += TWO_PI;
    double lo = start - EPS3;
    int b;
    if (p < lo || end + EPS3 < p) {
        b = 0;
        if (lo <= p + TWO_PI) b = (p + TWO_PI <= end + EPS3);
    } else {
        b = 1;
    }
    return b;
}

/* dmathe_KreisTangentenWinkel(flag, x1,y1,x2,y2) — tangent angle of a circle:
 * NormWinkel(Winkelstrecke(seg) ± pi/2), sign chosen by flag (confirmed from the
 * disassembly: flag==1 -> +pi/2 branch, else -pi/2; tail-returns NormWinkel). */
double dmathe_KreisTangentenWinkel(int flag, double x1, double y1, double x2, double y2)
{
    double w = dmathe_Winkelstrecke(x1, y1, x2, y2);
    if (flag == 1) return dmathe_NormWinkel(w + PI_2);
    return dmathe_NormWinkel(w - PI_2);
}

/* dmathe_PktAufBogen(px,py,cx,cy,startA,endA,dir) — is the point on the arc?
 *
 * Ghidra mis-decompiled this as a void function that discards RadAufBogen's
 * result; the raw disassembly (b5d0) shows otherwise. After the stack-slot
 * shuffles it computes:
 *     angle = Winkelstrecke(cx, cy, px, py)   // direction center->point
 *     return RadAufBogen(startA, endA, dir, angle)
 * and the trailing `add esp,0x34; ret` leaves RadAufBogen's eax (the bool)
 * untouched — i.e. it TAIL-RETURNS the predicate. So PktAufBogen tests whether
 * a point lies on a circular arc: take its angle about the center, then ask
 * whether that angle falls on the arc [startA,endA] in the given direction.
 * Arg order recovered from the push sequence: (px,py)=p0,p1 ; (cx,cy)=p2,p3 ;
 * startA=p4 ; endA=p5 ; dir = signed char p6 (movsbl). Returns bool. */
int dmathe_PktAufBogen(double px, double py, double cx, double cy,
                       double startA, double endA, int dir)
{
    double angle = dmathe_Winkelstrecke(cx, cy, px, py);
    return dmathe_RadAufBogen(startA, endA, dir, angle);
}

/* dmathe_PktAufStrecke(px,py,ax,ay,bx,by) — is point on segment a-b (eps) */
int dmathe_PktAufStrecke(double px, double py, double ax, double ay, double bx, double by)
{
    if (dmathe_InIntervall(py, ay, by)) {
        if (dmathe_InIntervall(px, ax, bx)) {
            double d1 = (py - ay) * (bx - ax);
            double d2 = (by - ay) * (px - ax);
            if (d1 < d2) return (d2 - d1) < EPS8;
            return (d1 - d2) < EPS8;
        }
    }
    return 0;
}
