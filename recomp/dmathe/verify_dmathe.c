/*
 * verify_dmathe.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Drives the dmathe_* geometry leaves over
 * a deterministic grid. Doubles emitted with full precision (tolerant compare);
 * ints/bools exact. The longdouble (st0) returns are read as double, exactly as
 * C++ callers narrow them.
 */
#include <stdio.h>
#include <stdint.h>

extern void   dmathe_TauscheD(double*, double*);
extern void   dmathe_RightPerpVect(double, double, double*);
extern void   dmathe_LeftPerpVect(double, double, double*);
extern void   dmathe_PunktDrehen(double*, double, double, double, double);
extern double dmathe_roundst(double, int) __asm__("_Z14dmathe_roundstdi");
extern double dmathe_NormWinkel(double);
extern double dmathe_Wirein(double);
extern double dmathe_VectorWinkel(double, double);
extern double dmathe_Distance(double, double, double, double);
extern double dmathe_Turn180Degree(double*, double);
extern double dmathe_CalcOeffWinkel(double, double, int);
extern int    dmathe_QuadGl(double, double, double, double*, double*);
extern _Bool  dmathe_InIntervall(double, double, double);
extern _Bool  dmathe_wlinks(double, double);
extern _Bool  dmathe_wrechts(double, double);
extern _Bool  dmathe_antiparallel(double, double);
extern double dmathe_Winkelstrecke(double, double, double, double);
extern _Bool  dmathe_SpGreater0(double, double, double, double, double, double);
extern _Bool  dmathe_RadAufBogen(double, double, int, double);
extern _Bool  dmathe_PktAufStrecke(double, double, double, double, double, double);
extern double dmathe_KreisTangentenWinkel(int, double, double, double, double);
extern _Bool  dmathe_PktAufBogen(double, double, double, double, double, double, int);

static int DI = 0, II = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }
static void I(int v){ printf("I %d %d\n", II++, v); }

int main(void)
{
    /* exact vector ops */
    static const double cc[] = { -7.5, -1.0, 0.0, 0.3, 1.0, 42.25, 1e6 };
    for (unsigned i = 0; i < sizeof(cc)/sizeof(cc[0]); i++)
    for (unsigned j = 0; j < sizeof(cc)/sizeof(cc[0]); j++) {
        double a = cc[i], b = cc[j], o[2];
        double s[2] = { a, b };
        dmathe_TauscheD(&s[0], &s[1]); D(s[0]); D(s[1]);
        dmathe_RightPerpVect(a, b, o); D(o[0]); D(o[1]);
        dmathe_LeftPerpVect(a, b, o);  D(o[0]); D(o[1]);
        double p[2] = { a, b };
        dmathe_PunktDrehen(p, 0.6, 0.8, 1.5, -2.5); D(p[0]); D(p[1]);
    }

    /* rounding */
    static const double rv[] = { -3.14159, -0.5, -0.49, 0.0, 0.5, 1.2345, 7.777, 99.5 };
    for (unsigned i = 0; i < sizeof(rv)/sizeof(rv[0]); i++)
        for (int n = 1; n <= 8; n++)
            D(dmathe_roundst(rv[i], n));

    /* angle math over a fine grid (steps clear of the 1e-3 eps boundary) */
    for (int k = -80; k <= 80; k++) {
        double a = k * 0.137;
        D(dmathe_NormWinkel(a));
        D(dmathe_Wirein(a));
        double v[2] = { a, -a*0.5 };
        D(dmathe_Turn180Degree(v, a)); D(v[0]); D(v[1]);
        I(dmathe_antiparallel(a, a*0.7));
    }
    for (int i = -12; i <= 12; i++)
    for (int j = -12; j <= 12; j++) {
        double x = i * 0.83, y = j * 0.83;
        D(dmathe_VectorWinkel(x, y));
        D(dmathe_Distance(x, y, -y, x));
        I(dmathe_wlinks(i*0.21+0.5, j*0.21));
        I(dmathe_wrechts(i*0.21+0.5, j*0.21));
        I(dmathe_InIntervall(i*0.5, j*0.5 - 2.0, j*0.5 + 2.0));
    }

    /* opening angle, both flags */
    for (int f = 0; f <= 2; f++)
        for (int i = -10; i <= 10; i++)
        for (int j = -10; j <= 10; j++)
            D(dmathe_CalcOeffWinkel(i*0.37, j*0.41, f));

    /* quadratic solver */
    static const double qa[] = { -2.0, -0.5, 0.0007, 1.0, 3.5 };
    static const double qb[] = { -4.0, -1.0, 0.0, 2.0, 5.0 };
    static const double qc[] = { -3.0, 0.0, 1.0, 4.0 };
    for (unsigned i = 0; i < sizeof(qa)/sizeof(qa[0]); i++)
    for (unsigned j = 0; j < sizeof(qb)/sizeof(qb[0]); j++)
    for (unsigned k = 0; k < sizeof(qc)/sizeof(qc[0]); k++) {
        double x1 = 0, x2 = 0;
        int n = dmathe_QuadGl(qa[i], qb[j], qc[k], &x1, &x2);
        I(n); D(x1); D(x2);
    }

    /* segment angle + dot-product sign + arc/segment membership */
    for (int i = -10; i <= 10; i++)
    for (int j = -10; j <= 10; j++) {
        double x = i * 0.77, y = j * 0.77;
        D(dmathe_Winkelstrecke(0.0, 0.0, x, y));
        I(dmathe_SpGreater0(x, y, y, -x, 1.0, 0.5));
    }
    for (int s = 0; s <= 12; s++)
    for (int e = 0; e <= 12; e++)
    for (int d = -1; d <= 1; d += 2) {
        double start = s * 0.5, end = e * 0.5, p = (s + e) * 0.21;
        I(dmathe_RadAufBogen(start, end, d, p));
    }
    for (int i = -6; i <= 6; i++)
    for (int j = -6; j <= 6; j++) {
        double px = i * 0.5, py = j * 0.5;
        I(dmathe_PktAufStrecke(px, py, -3.0, -3.0, 3.0, 3.0));   /* the y=x diagonal */
        I(dmathe_PktAufStrecke(px, py, -2.0,  1.0, 2.0, 1.0));   /* horizontal y=1   */
    }
    for (int f = 0; f <= 1; f++)
    for (int i = -8; i <= 8; i++)
    for (int j = -8; j <= 8; j++)
        D(dmathe_KreisTangentenWinkel(f, 0.0, 0.0, i * 0.6, j * 0.6));

    /* point-on-arc: point around a center, against several arc ranges + dirs */
    static const double arc[][2] = { {0.0, 1.5}, {0.5, 3.0}, {3.0, 0.5},
                                     {-1.0, 1.0}, {2.0, 5.0}, {0.0, 6.0} };
    for (int i = -7; i <= 7; i++)
    for (int j = -7; j <= 7; j++) {
        double px = i * 0.6, py = j * 0.6;     /* point about center (0.2,-0.3) */
        for (unsigned a = 0; a < sizeof(arc)/sizeof(arc[0]); a++)
            for (int dir = -1; dir <= 1; dir++)
                I(dmathe_PktAufBogen(px, py, 0.2, -0.3, arc[a][0], arc[a][1], dir));
    }
    return 0;
}
