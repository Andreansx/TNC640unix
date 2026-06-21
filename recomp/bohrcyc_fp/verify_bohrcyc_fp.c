/*
 * verify_bohrcyc_fp.c — one harness, compiled two ways:
 *   - i386  : linked against the REAL trimmed libEp90_Bohrcyc.so (run under qemu-i386)
 *   - arm64 : linked against libEp90_Bohrcyc_fp_rebuilt.c (run native on the M2)
 * Emits one structured line per test vector; compare_fp.py diffs the two with a
 * tight FP tolerance (exact for the integer/boolean return codes).
 *
 * Line formats:
 *   ENT <idx> <ret> <result_bits_hex> <result_%.17g>
 *   WG  <idx> <ret> <margin_%.17g> <tol_%.17g>
 */
#include <stdio.h>
#include <math.h>
#include <stdint.h>

extern int BCYC_EntnormiereWinkel(double *winkel, double ref, double halbbereich);
extern int BCYC_WinkelGleich(double a, double b, double tol);

static uint64_t bits(double d) { union { double d; uint64_t u; } x; x.d = d; return x.u; }

int main(void)
{
    /* ---- BCYC_EntnormiereWinkel: grid over (angle, ref, halfrange) ---- */
    int eidx = 0;
    for (int ai = -40; ai <= 40; ai++) {
        for (int ri = -8; ri <= 8; ri++) {
            for (int hi = 0; hi <= 8; hi++) {
                double v    = ai * 0.37;     /* angle, ~[-14.8, 14.8] */
                double ref  = ri * 1.13;     /* reference angle       */
                double half = hi * 0.41;     /* half range            */
                double w = v;
                int ret = BCYC_EntnormiereWinkel(&w, ref, half);
                printf("ENT %d %d %016llx %.17g\n",
                       eidx++, ret, (unsigned long long)bits(w), w);
            }
        }
    }

    /* ---- BCYC_WinkelGleich: grid over (a, b) at several tolerances ---- */
    int widx = 0;
    static const double tols[] = { 1e-9, 1e-6, 1e-3, 1e-1 };
    for (unsigned t = 0; t < sizeof(tols)/sizeof(tols[0]); t++) {
        double tol = tols[t];
        for (int ai = -60; ai <= 60; ai++) {
            for (int bi = -60; bi <= 60; bi++) {
                double a = ai * 0.11;
                double b = bi * 0.11;
                int ret = BCYC_WinkelGleich(a, b, tol);
                /* recompute the deciding margin locally for boundary reporting */
                double sa = sin(a), ca = cos(a), sb = sin(b), cb = cos(b);
                double ds = fabs(sa - sb), dc = fabs(ca - cb);
                double margin = ds > dc ? ds : dc;
                printf("WG %d %d %.17g %.17g\n", widx++, ret, margin, tol);
            }
        }
    }
    return 0;
}
