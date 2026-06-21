/*
 * verify_aequi.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Drives the length-tolerance accessors
 * over double pairs and anz_same_level over per-arch linked lists of varying
 * length. Doubles full-precision (tolerant), ints exact. Deterministic.
 */
#include <stdio.h>
#include <stdint.h>

typedef struct Akopf { void *f0; struct Akopf *next; } Akopf;

extern double get_laengentoleranz(double, double)    __asm__("_Z19get_laengentoleranzdd");
extern double AEQ_GetLaengentoleranz(double, double) __asm__("_Z22AEQ_GetLaengentoleranzdd");
extern int    anz_same_level(const void*)            __asm__("_Z14anz_same_levelP5akopf");

static int DI = 0, II = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }
static void I(int v){ printf("I %d %d\n", II++, v); }

int main(void)
{
    static const double dv[] = { -3.5, -1.0, 0.0, 0.001, 1.0, 25.4, 1e9 };
    for (unsigned i = 0; i < sizeof(dv)/sizeof(dv[0]); i++)
    for (unsigned j = 0; j < sizeof(dv)/sizeof(dv[0]); j++) {
        D(get_laengentoleranz(dv[i], dv[j]));
        D(AEQ_GetLaengentoleranz(dv[i], dv[j]));
    }

    /* linked lists of length 0..16 */
    static Akopf nodes[16];
    for (int n = 0; n <= 16; n++) {
        for (int k = 0; k < n; k++) nodes[k].next = (k + 1 < n) ? &nodes[k + 1] : 0;
        I(anz_same_level(n ? &nodes[0] : 0));
    }
    return 0;
}
