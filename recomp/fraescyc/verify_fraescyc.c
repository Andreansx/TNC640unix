/*
 * verify_fraescyc.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Builds a flat tec_cycfraes_rt and drives
 * the four FCYC_* accessors over varied field values + scalar inputs.
 * Doubles full-precision (tolerant); ints/chars exact.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

extern double      FCYC_FraesTiefe(const void*);
extern double      FCYC_AbhebeLaenge(const void*, double);
extern signed char FCYC_VorschubArt(const void*, int, int);

static int DI = 0, II = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }
static void I(int v){ printf("I %d %d\n", II++, v); }

static void setd(unsigned char *p, int o, double v){ memcpy(p+o, &v, 8); }
static void seti(unsigned char *p, int o, int32_t v){ memcpy(p+o, &v, 4); }

int main(void)
{
    static const double dv[] = { -3.0, -0.5, 0.0, 0.5, 2.0, 1e38 };
    static const int8_t flags[] = { 0, 1 };
    unsigned char s[0x1a0];

    for (unsigned a = 0; a < 6; a++)         /* +0x164 / +0x148 / +0x170 */
    for (unsigned b = 0; b < 6; b++)         /* +0x94 threshold         */
    for (int f161 = 0; f161 <= 1; f161++)
    for (int f10  = 0; f10  <= 1; f10++)
    for (int f16c = 0; f16c <= 1; f16c++)
    for (int f192 = 0; f192 <= 1; f192++) {
        memset(s, 0, sizeof s);
        setd(s, 0x74,  3.75);
        setd(s, 0x94,  dv[b]);
        setd(s, 0x148, dv[a]);
        setd(s, 0x164, dv[a]);
        setd(s, 0x170, dv[a]);
        seti(s, 0x10,  f10);
        seti(s, 0x16c, f16c);
        s[0x161] = (unsigned char)f161;
        s[0x192] = (unsigned char)f192;
        D(FCYC_FraesTiefe(s));
        D(FCYC_AbhebeLaenge(s, 1.25));
        for (int x = 0; x <= 2; x++)
        for (int y = 0; y <= 3; y++)
            I(FCYC_VorschubArt(s, x, y));
    }
    return 0;
}
