/*
 * libEp90_Cyckkorr_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of leaf functions of the
 * proprietary i386 libEp90_Cyckkorr.so (cycle-correction geometry):
 *   - renormiere_punkt : rotate a point by a quadrant (0/90/180/270°) using the
 *     bit-exact sin/cos residuals of those angles; two flag variants;
 *   - ckk_uebertrage_attribute : copy a fixed set of attribute fields between
 *     two geotec elements (flat struct, no indirection).
 *
 * Source of truth: work/re/out/libEp90_Cyckkorr.so.decomp.c (Ghidra). The three
 * rotation constants are the ~1e-16 FP residuals of sin(180°)/cos(90°)/sin(270°).
 */
#include <string.h>
#include <stdint.h>

#define CK_S180  1.2246467991473532e-16    /* DAT_42d48 (sin 180°)  */
#define CK_C90   6.123233995736766e-17     /* DAT_42d40 (cos 90°)   */
#define CK_S270 (-1.8369701987210297e-16)  /* DAT_42d38 (sin 270°)  */

/* renormiere_punkt(*px, *py, quad, flag): rotate (*px,*py) by the quadrant.
 * quad must be 1..4. flag selects the rotation sense / sign convention. */
void renormiere_punkt(double *px, double *py, int quad, int flag) __asm__("_Z16renormiere_punktPdS_6hsr_ati");
void renormiere_punkt(double *px, double *py, int quad, int flag)
{
    if ((unsigned)(quad - 1) > 3) return;
    double x = *px, y = *py;
    double s, c;   /* (s = dVar3, c = dVar4 in the decompile) */
    if (flag != 1) {
        if (quad == 3)      { c = -1.0; s = CK_S180; }
        else if (quad == 4) { s = -1.0; c = CK_S270; }
        else if (quad == 2) { s =  1.0; c = CK_C90;  }
        else {                                   /* quad == 1: identity */
            *py = x * 0.0 + y;
            *px = x - y * 0.0;
            return;
        }
        *py = c * y + s * x;
        *px = c * x - s * y;
        return;
    }
    /* flag == 1 */
    if (quad == 1)      { c = -1.0; s = CK_S180; }
    else if (quad == 4) { s = -1.0; c = CK_S270; }
    else if (quad == 2) { s =  1.0; c = CK_C90;  }
    else                { s =  0.0; c = 1.0;     }   /* quad == 3 */
    *py = s * x + c * y;
    *px = -(c * x - s * y);
}

/* ckk_uebertrage_attribute(src, dst): copy the attribute block of a geotec from
 * src to dst — a fixed set of fields by flat offset (no indirection). */
static void cp(void *d, const void *s, int off, int n) { memcpy((char*)d+off, (const char*)s+off, n); }
void ckk_uebertrage_attribute(void *src, void *dst) __asm__("_Z24ckk_uebertrage_attributeP6geotecS0_");
void ckk_uebertrage_attribute(void *src, void *dst)
{
    cp(dst, src, 0x64, 8);
    cp(dst, src, 0x6c, 1);
    cp(dst, src, 0x6d, 1);
    cp(dst, src, 0x70, 8);
    cp(dst, src, 0x78, 4);
    cp(dst, src, 0x7c, 1);
    cp(dst, src, 0x84, 4);
    cp(dst, src, 0x88, 8);
}
