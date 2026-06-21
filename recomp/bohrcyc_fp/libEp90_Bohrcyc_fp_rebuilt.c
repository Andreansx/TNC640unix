/*
 * libEp90_Bohrcyc_fp_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the FLOATING-POINT leaf
 * functions of the proprietary i386 libEp90_Bohrcyc.so. These were explicitly
 * EXCLUDED from the byte-identical proof set because they compute transcendental
 * math (sincos) / cross the x87-vs-SSE double-rounding boundary, so the machine
 * code cannot be made bit-identical across the ISA boundary. Instead we prove
 * OBSERVABLE EQUIVALENCE: same integer/boolean return codes (exactly) and same
 * double results to within a tight FP tolerance (~1 ULP), differentially against
 * the genuine i386 .so executed under qemu-i386.
 *
 * Source of truth: work/re/out/libEp90_Bohrcyc.so.decomp.c (Ghidra) +
 * the two FP constants lifted verbatim from .rodata:
 *   DAT_00012288 = 0x400921fb54442d18 = pi   (3.14159265358979311600)
 *   DAT_00012290 = 0x401921fb54442d18 = 2*pi (6.28318530717958623200)
 * Both are bit-exact IEEE-754 doubles; expressed here as exact hex-float
 * literals so no compile-time rounding can perturb them.
 */
#include <math.h>

/* bit-exact constants from the binary's .rodata */
#define BCYC_PI     0x1.921fb54442d18p+1   /* 3.141592653589793 */
#define BCYC_TWO_PI 0x1.921fb54442d18p+2   /* 6.283185307179586 */

/*
 * BCYC_EntnormiereWinkel(double *winkel, double ref, double halbbereich)
 *
 * "De-normalize angle": if *winkel lies outside the [ref-(halbbereich+pi),
 * ref+(halbbereich+pi)] window, shift it by exactly one full turn (2*pi)
 * toward the reference and report that a correction happened.
 *
 * Decompiled control flow (libEp90_Bohrcyc.so @ 0x00000df0):
 *   ret = 0; v = *winkel;
 *   if (halbbereich + pi <= |v - ref|) {
 *       if (v <= ref) { *winkel = v + 2pi; return 1; }   // below window -> +turn
 *       ret = 1; *winkel = v - 2pi;                       // above window -> -turn
 *   }
 *   return ret;
 *
 * This is pure IEEE add/sub/compare on doubles — no transcendental — so it is
 * within one double-rounding ULP of the x87 original (typically bit-identical).
 */
int BCYC_EntnormiereWinkel(double *winkel, double ref, double halbbereich)
{
    int ret = 0;
    double v = *winkel;
    if (halbbereich + BCYC_PI <= fabs(v - ref)) {
        if (v <= ref) {
            *winkel = v + BCYC_TWO_PI;
            return 1;
        }
        ret = 1;
        *winkel = v - BCYC_TWO_PI;
    }
    return ret;
}

/*
 * BCYC_WinkelGleich(double a, double b, double tol)
 *
 * "Angles equal?": true iff a and b have the same sine AND the same cosine to
 * within tol. Comparing sin/cos rather than the raw angle makes the test
 * insensitive to +/-2*pi wrapping (e.g. 0 and 2*pi compare equal).
 *
 * Decompiled (libEp90_Bohrcyc.so @ 0x00000e70):
 *   sincos(b, &sin_b, &cos_b);
 *   sincos(a, &sin_a, &cos_a);
 *   return |sin_a - sin_b| < tol && |cos_a - cos_b| < tol;
 *
 * sincos is transcendental; the i386 build uses the x87 fsincos path while
 * ARM64 uses the libm double path, so individual sin/cos values differ by ~1
 * ULP. The OBSERVABLE output (the boolean) is identical except for inputs whose
 * sin/cos margin sits within ~ULPs of tol; the harness avoids the exact
 * boundary and the comparator reports any residual margin cases explicitly.
 */
int BCYC_WinkelGleich(double a, double b, double tol)
{
    /* The original calls glibc sincos(); computing sin()/cos() separately
     * yields the identical pair on a given platform, and keeps this portable
     * across macOS clang (no GNU sincos) and Linux gcc. */
    double sin_b = sin(b), cos_b = cos(b);
    double sin_a = sin(a), cos_a = cos(a);
    return (fabs(sin_a - sin_b) < tol) && (fabs(cos_a - cos_b) < tol);
}
