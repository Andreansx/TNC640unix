/*
 * libtncMetaValue_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the leaf C++ class methods of
 * the proprietary i386 libtncMetaValue.so. These are genuine C++ member
 * functions (mangled Itanium-ABI symbols, `this` pointer), which is why they
 * sat outside the byte-identical set. We instead prove OBSERVABLE EQUIVALENCE:
 * for the same logical inputs, the native methods return the same values as the
 * real i386 methods executed under qemu-i386 (exact for ints, ~1 ULP for the
 * unit-conversion doubles).
 *
 * Each function is bound to its exact mangled symbol via an __asm__ label so it
 * is a drop-in replacement. Source of truth:
 * work/re/out/libtncMetaValue.so.decomp.c (Ghidra) + the two unit constants
 * lifted from .rodata (file off 0x84a0 = 2.54, 0x84a8 = 25.4).
 *
 * EXCLUDED (not leaves): the TncMetaValue Is.../Get... members that dispatch
 * virtually through the pImpl vtable (IsInteger/IsReal/IsCardinal/IsParameter/
 * GetMemorySize) - they require a live impl object's vtable, not reproducible
 * standalone.
 */
#include <string.h>
#include "metaval_layout.h"

/* bit-exact unit factors from the binary's .rodata */
#define MV_MM_PER_FEED 2.54    /* DAT_000184a0, exact IEEE double */
#define MV_MM_PER_POS  25.4    /* DAT_000184a8, exact IEEE double */

/* ===================== static methods (no `this`) ===================== */

/* TncMetaValue::InchPrecision(bool, double) -> bool : b && 1.0 < d */
int  mv_InchPrecision(int b, double d) __asm__("_ZN12TncMetaValue13InchPrecisionEbd");
int  mv_InchPrecision(int b, double d) { return b && (1.0 < d); }

/* TncMetaValue::ToMetricFeedValue(bool) -> long double : m ? 2.54 : 1.0 */
double mv_ToMetricFeedValue(int m) __asm__("_ZN12TncMetaValue17ToMetricFeedValueEb");
double mv_ToMetricFeedValue(int m) { return m ? MV_MM_PER_FEED : 1.0; }

/* TncMetaValue::ToMetricPosValue(bool) -> long double : m ? 25.4 : 1.0 */
double mv_ToMetricPosValue(int m) __asm__("_ZN12TncMetaValue16ToMetricPosValueEb");
double mv_ToMetricPosValue(int m) { return m ? MV_MM_PER_POS : 1.0; }

/* TncMetaValue::ToNonMetricFeedValue(bool) -> long double : 1 / metric-feed */
double mv_ToNonMetricFeedValue(int m) __asm__("_ZN12TncMetaValue20ToNonMetricFeedValueEb");
double mv_ToNonMetricFeedValue(int m) { return 1.0 / (m ? MV_MM_PER_FEED : 1.0); }

/* TncMetaValue::ToNonMetricPosValue(bool) -> long double : 1 / metric-pos */
double mv_ToNonMetricPosValue(int m) __asm__("_ZN12TncMetaValue19ToNonMetricPosValueEb");
double mv_ToNonMetricPosValue(int m) { return 1.0 / (m ? MV_MM_PER_POS : 1.0); }

/* ============ CycMetaValue accessors (`this`, in-object fields) ============ */

/* CycMetaValue::IsCardinal() const -> 0   (IsInteger shares this code/value) */
int mv_CycIsCardinal(const void *t) __asm__("_ZNK12CycMetaValue10IsCardinalEv");
int mv_CycIsCardinal(const void *t) { (void)t; return 0; }

int mv_CycIsInteger(const void *t) __asm__("_ZNK12CycMetaValue9IsIntegerEv");
int mv_CycIsInteger(const void *t) { (void)t; return 0; }

/* CycMetaValue::IsReal() const -> 1 */
int mv_CycIsReal(const void *t) __asm__("_ZNK12CycMetaValue6IsRealEv");
int mv_CycIsReal(const void *t) { (void)t; return 1; }

/* CycMetaValue::GetArraySize() const -> 1 */
int mv_CycGetArraySize(const void *t) __asm__("_ZNK12CycMetaValue12GetArraySizeEv");
int mv_CycGetArraySize(const void *t) { (void)t; return 1; }

/* CycMetaValue::IsSigned() const -> *(double*)(this+0xc) < 0.0 */
int mv_CycIsSigned(const void *t) __asm__("_ZNK12CycMetaValue8IsSignedEv");
int mv_CycIsSigned(const void *t)
{
    double d;
    memcpy(&d, (const unsigned char *)t + CYC_SIGNED_OFF, sizeof d);
    return d < 0.0;
}

/* CycMetaValue::GetTextLength() const -> *(uint32*)(this+0x24) */
unsigned mv_CycGetTextLength(const void *t) __asm__("_ZNK12CycMetaValue13GetTextLengthEv");
unsigned mv_CycGetTextLength(const void *t)
{
    uint32_t v;
    memcpy(&v, (const unsigned char *)t + CYC_TEXTLEN_OFF, sizeof v);
    return v;
}

/* ========= TncMetaValue accessors (`this`, pImpl pointer indirection) ========= */

/* TncMetaValue::IsSigned() const :
 *   impl ? ((fmt->flags >> 5) & 1) : 0 */
int mv_TncIsSigned(const void *t) __asm__("_ZNK12TncMetaValue8IsSignedEv");
int mv_TncIsSigned(const void *t)
{
    const TncObj *o = (const TncObj *)t;
    if (o->impl != 0)
        return (o->fmt->flags >> 5) & 1;
    return 0;
}

/* TncMetaValue::GetTextLength() const : fmt->textlen (16-bit) */
unsigned mv_TncGetTextLength(const void *t) __asm__("_ZNK12TncMetaValue13GetTextLengthEv");
unsigned mv_TncGetTextLength(const void *t)
{
    const TncObj *o = (const TncObj *)t;
    return o->fmt->textlen;
}

/* TncMetaValue::GetArraySize() const : impl != 0 */
int mv_TncGetArraySize(const void *t) __asm__("_ZNK12TncMetaValue12GetArraySizeEv");
int mv_TncGetArraySize(const void *t)
{
    const TncObj *o = (const TncObj *)t;
    return o->impl != 0;
}

/* TncMetaValue::IsSignedQ() const :
 *   (impl && fmt->type == 6) ? ((fmt->flags >> 5) & 1) : 0 */
int mv_TncIsSignedQ(const void *t) __asm__("_ZNK12TncMetaValue9IsSignedQEv");
int mv_TncIsSignedQ(const void *t)
{
    const TncObj *o = (const TncObj *)t;
    if (o->impl != 0 && o->fmt->type == 6)
        return (o->fmt->flags >> 5) & 1;
    return 0;
}
