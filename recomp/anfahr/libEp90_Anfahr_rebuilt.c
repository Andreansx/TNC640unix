/*
 * libEp90_Anfahr_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of two pure-FP leaves of the
 * proprietary i386 libEp90_Anfahr.so (approach-motion geometry):
 *   - EckenWinkel        : corner angle between two directions (±2π fold);
 *   - get_einfahr_radius : entry-radius clamp.
 *
 * get_einfahr_radius was a DISASSEMBLY RECOVERY: Ghidra decompiled it as a void
 * function whose branches all `return;` (it lost the x87 return value). Tracing
 * st0 through the real .text (3a20) gives the actual three-way result. Proven
 * OBSERVABLY EQUIVALENT to the real i386 .so under qemu-i386.
 *
 * Constants from .rodata: DAT_23510=π, DAT_23518=2π (EckenWinkel);
 * DAT_23574=3.0(float, C), DAT_23570=5.0(float, D) (get_einfahr_radius).
 */
#include <math.h>

#define ANF_PI  0x1.921fb54442d18p+1
#define ANF_2PI 0x1.921fb54442d18p+2

/* EckenWinkel(a1, a2, eckeType) — corner angle. eckeType (an ecke_st enum
 * passed as a 4-byte value) selects the fold direction:
 *   == 0x20 : straight  -> π
 *   <  0x20 : if |a1-a2| > π, fold to 2π-|a1-a2|
 *   >  0x20 : if |a1-a2| < π, fold to 2π-|a1-a2| */
double EckenWinkel(double a1, double a2, int eckeType) __asm__("_Z11EckenWinkeldd7ecke_st");
double EckenWinkel(double a1, double a2, int eckeType)
{
    if (eckeType == 0x20) return ANF_PI;
    double diff = fabs(a1 - a2);
    if (eckeType < 0x20) {
        if (ANF_PI < diff) diff = ANF_2PI - diff;
    } else {
        if (diff < ANF_PI) diff = ANF_2PI - diff;
    }
    return diff;
}

/* x87 fcmovnbe min: returns the smaller (move st1 into st0 when st0 > st1) */
static double anf_min(double a, double b) { return (a > b) ? b : a; }

/* get_einfahr_radius(p1, p2, p3) with C = 3.0, D = 5.0:
 *   p1 <= p3            -> min(D, C·p2)
 *   p1 <  p3 + C·p2     -> min(2·p1, C·p2)
 *   else               -> p1
 * (the last branch returns p1 — the i386 `fstp st(1)` leaves p1 in st0 after
 * the pop, which Ghidra's "void" decompile hid.) */
double get_einfahr_radius(double p1, double p2, double p3) __asm__("_Z18get_einfahr_radiusddd");
double get_einfahr_radius(double p1, double p2, double p3)
{
    double Cp2 = 3.0 * p2;
    if (p1 <= p3)        return anf_min(5.0, Cp2);
    if (p1 < p3 + Cp2)   return anf_min(p1 + p1, Cp2);
    return p1;
}
