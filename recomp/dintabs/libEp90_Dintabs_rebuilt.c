/*
 * libEp90_Dintabs_rebuilt.c — native re-implementation of the pure DIN/ANSI
 * thread-table lookup functions from the proprietary i386 libEp90_Dintabs.so,
 * recovered via Ghidra (work/re/out/libEp90_Dintabs.so.decomp.c) + targeted
 * i386 disassembly (rizin). Compiles to native Apple-Silicon arm64 / aarch64.
 *
 * Scope = pure leaf functions over the embedded static tables (no C++ classes,
 * no globals, no external calls). The exact table bytes are lifted verbatim
 * from the original .so by extract_tables.py -> dintabs_tables.h, so every
 * returned value is bit-identical to the original by construction.
 *
 * Functions reproduced (exported under the original C++-mangled names so this
 * is a drop-in for the real .so):
 *   GetNennd(unsigned char, unsigned short)            -> nominal-diameter lookup
 *   hole_din_werte_freistich_{ab,cd,ef,g}(double,**)   -> thread-relief table scan
 *   NenndTblVgl(double,double,double,int,double*,double*) -> nominal-table compare
 *
 * Verified byte-identical vs the real i386 .so under qemu-i386 — see
 * build_and_verify_dintabs.sh.
 */
#include <math.h>
#include "dintabs_tables.h"

typedef unsigned char u8;

/* The original returns its result in the x87 st(0) register as a widened
 * double (Ghidra types it `longdouble`); the value is always a plain double
 * loaded from a table or copied from a caller array, so declaring `double`
 * here is exact. */

/* ---- GetNennd: switch on thread-type, index the matching double table ---- */
double GetNennd(u8 type, unsigned short idx) asm("_Z8GetNenndht");
double GetNennd(u8 type, unsigned short idx)
{
    switch (type) {
    case 0x09: return ((const double*)NennDIN11)[idx];
    case 0x0a: return ((const double*)NennDIN2999)[idx];
    case 0x0b: return *(const double*)(NennDIN259 + (unsigned)idx * 8);
    case 0x0d: return ((const double*)NennUNC)[idx];
    case 0x0e: return ((const double*)NennUNF)[idx];
    case 0x0f: return ((const double*)NennUNEF)[idx];
    case 0x10: return ((const double*)NennNPT)[idx];
    case 0x11: return ((const double*)NennNPTF)[idx];
    case 0x12: return ((const double*)NennNPSC)[idx];
    case 0x13: return ((const double*)NennNPSF)[idx];
    default:   return 0.0;
    }
}

/* ---- freistich (thread-relief) table scans ----
 * Each table is an array of 0x30-byte din_freistich_rt entries. The scan
 * returns a pointer to the matching entry (caller compares the 0x30 bytes). */

/* form ab: ascending 0..3, match |x - entry[+8]| < tol */
int hole_din_werte_freistich_ab(double x, void **out)
    asm("_Z27hole_din_werte_freistich_abdPP16din_freistich_rt");
int hole_din_werte_freistich_ab(double x, void **out)
{
    for (int i = 0; i < 4; i++) {
        const unsigned char *e = frei_ab_werte + i * 0x30;
        if (fabs(x - *(const double*)(e + 8)) < DAT_freistich_tol) {
            *out = (void*)(frei_ab_werte + i * 0x30);
            return 1;
        }
    }
    return 0;
}

/* form cd: ascending 0..2, match |x - entry[+0x20]| < tol */
int hole_din_werte_freistich_cd(double x, void **out)
    asm("_Z27hole_din_werte_freistich_cddPP16din_freistich_rt");
int hole_din_werte_freistich_cd(double x, void **out)
{
    for (int i = 0; i < 3; i++) {
        const unsigned char *e = frei_cd_werte + i * 0x30;
        if (fabs(x - *(const double*)(e + 0x20)) < DAT_freistich_tol) {
            *out = (void*)(frei_cd_werte + i * 0x30);
            return 1;
        }
    }
    return 0;
}

/* form ef: descending 5..0, match entry[+0] <= x */
int hole_din_werte_freistich_ef(double x, void **out)
    asm("_Z27hole_din_werte_freistich_efdPP16din_freistich_rt");
int hole_din_werte_freistich_ef(double x, void **out)
{
    for (int i = 5; i >= 0; i--) {
        const unsigned char *e = frei_ef_werte + i * 0x30;
        if (*(const double*)e <= x) {
            *out = (void*)(frei_ef_werte + i * 0x30);
            return 1;
        }
    }
    return 0;
}

/* form g: descending 24..0, match entry[+0] <= x */
int hole_din_werte_freistich_g(double x, void **out)
    asm("_Z26hole_din_werte_freistich_gdPP16din_freistich_rt");
int hole_din_werte_freistich_g(double x, void **out)
{
    for (int i = 24; i >= 0; i--) {
        const unsigned char *e = frei_g_werte + i * 0x30;
        if (*(const double*)e <= x) {
            *out = (void*)(frei_g_werte + i * 0x30);
            return 1;
        }
    }
    return 0;
}

/* ---- NenndTblVgl: walk caller's nominal table, return the matching entry ----
 * The returned value is always one of tbl[] (param_5), copied not computed, so
 * it is bit-exact; only the branch decisions use arithmetic. */
double NenndTblVgl(double tol, double pitch, double factor, int n,
                   double *tbl, double *cmp) asm("_Z11NenndTblVgldddiPdS_");
double NenndTblVgl(double tol, double pitch, double factor, int n,
                   double *tbl, double *cmp)
{
    double p = pitch;
    if (p == 0.0) p = 1.0;
    double first = tbl[0];
    if (n > 0) {
        int i = 0;
        double cur = first;
        for (;;) {
            double delta = (cmp[i] / p) * factor;
            if (!(cur - (delta + delta) < tol + DAT_nenndtbl_eps))
                break;
            i++;
            if (n == i) return cur;
            cur = tbl[i];
        }
        if (i != 0) first = tbl[i - 1];
    }
    return first;
}
