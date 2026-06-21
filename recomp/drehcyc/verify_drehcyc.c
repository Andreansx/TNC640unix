/*
 * verify_drehcyc.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Drives is_aufmass_aktiv over flag bits,
 * (m1,m2) magnitudes and tolerances. The aufmass_rt struct is passed BY VALUE.
 * Bool result read as _Bool (i386 `setbe al` leaves the upper eax undefined).
 */
#include <stdio.h>
#include "drehcyc_layout.h"

extern _Bool is_aufmass_aktiv(aufmass_rt, double) __asm__("_Z16is_aufmass_aktiv10aufmass_rtd");

static int II = 0;
static void I(int v){ printf("I %d %d\n", II++, v); }

int main(void)
{
    static const double dv[]  = { -3.0, -0.5, 0.0, 0.5, 2.0 };
    static const double tols[] = { 0.0, 0.5, 1.0, 2.5 };
    static const unsigned char flags[] = { 0x00, 0x10, 0x20, 0x30, 0x01, 0x31, 0xff };

    for (unsigned f = 0; f < sizeof(flags)/sizeof(flags[0]); f++)
    for (unsigned a = 0; a < 5; a++)
    for (unsigned b = 0; b < 5; b++)
    for (unsigned t = 0; t < sizeof(tols)/sizeof(tols[0]); t++) {
        aufmass_rt am;
        am.flag = flags[f];
        am.m1 = dv[a];
        am.m2 = dv[b];
        I(is_aufmass_aktiv(am, tols[t]));
    }
    return 0;
}
