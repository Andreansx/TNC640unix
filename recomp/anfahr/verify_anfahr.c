/*
 * verify_anfahr.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Drives EckenWinkel over angle pairs +
 * ecke types, and get_einfahr_radius over (p1,p2,p3) triples covering all three
 * result branches. Doubles full-precision (tolerant compare).
 */
#include <stdio.h>
#include <stdint.h>

extern double EckenWinkel(double, double, int)        __asm__("_Z11EckenWinkeldd7ecke_st");
extern double get_einfahr_radius(double, double, double) __asm__("_Z18get_einfahr_radiusddd");

static int DI = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }

int main(void)
{
    /* EckenWinkel over angle pairs and ecke types (below/at/above 0x20) */
    static const int types[] = { 0, 0x10, 0x1f, 0x20, 0x21, 0x40 };
    for (int i = -30; i <= 30; i++)
    for (int j = -30; j <= 30; j++)
        for (unsigned t = 0; t < sizeof(types)/sizeof(types[0]); t++)
            D(EckenWinkel(i * 0.21, j * 0.21, types[t]));

    /* get_einfahr_radius: sweep p1,p2,p3 to hit all branches */
    static const double pv[] = { -1.0, 0.0, 0.5, 1.0, 2.0, 5.0, 10.0 };
    for (unsigned a = 0; a < sizeof(pv)/sizeof(pv[0]); a++)
    for (unsigned b = 0; b < sizeof(pv)/sizeof(pv[0]); b++)
    for (unsigned c = 0; c < sizeof(pv)/sizeof(pv[0]); c++)
        D(get_einfahr_radius(pv[a], pv[b], pv[c]));
    return 0;
}
