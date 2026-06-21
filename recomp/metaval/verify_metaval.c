/*
 * verify_metaval.c — one harness, compiled two ways:
 *   - i386  : linked against the REAL trimmed libtncMetaValue.so (run under qemu-i386)
 *   - arm64 : linked against libtncMetaValue_rebuilt.c (run native on the M2)
 * The class methods are reached by their exact mangled Itanium-ABI symbols
 * (asm labels). Objects are built per-arch from identical LOGICAL inputs.
 *
 * Line formats:
 *   I  <idx> <int>
 *   D  <idx> <bits_hex> <%.17g>
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "metaval_layout.h"

/* Methods whose C++ return type is `bool` are declared `_Bool` so the harness
 * reads only the ABI-defined low byte (al). The i386 codegen for some of these
 * (e.g. CycMetaValue::IsSigned: fcomi + setb al) leaves the upper 24 bits of
 * eax holding load-address-dependent garbage; per the C++ ABI a bool return
 * only defines al, so reading wider would compare non-deterministic bits. */
/* static methods */
extern _Bool  InchPrecision(int, double)     __asm__("_ZN12TncMetaValue13InchPrecisionEbd");
extern double ToMetricFeedValue(int)         __asm__("_ZN12TncMetaValue17ToMetricFeedValueEb");
extern double ToMetricPosValue(int)          __asm__("_ZN12TncMetaValue16ToMetricPosValueEb");
extern double ToNonMetricFeedValue(int)      __asm__("_ZN12TncMetaValue20ToNonMetricFeedValueEb");
extern double ToNonMetricPosValue(int)       __asm__("_ZN12TncMetaValue19ToNonMetricPosValueEb");
/* CycMetaValue */
extern _Bool    CycIsCardinal(const void*)   __asm__("_ZNK12CycMetaValue10IsCardinalEv");
extern _Bool    CycIsInteger(const void*)    __asm__("_ZNK12CycMetaValue9IsIntegerEv");
extern _Bool    CycIsReal(const void*)       __asm__("_ZNK12CycMetaValue6IsRealEv");
extern int      CycGetArraySize(const void*) __asm__("_ZNK12CycMetaValue12GetArraySizeEv");
extern _Bool    CycIsSigned(const void*)     __asm__("_ZNK12CycMetaValue8IsSignedEv");
extern unsigned CycGetTextLength(const void*)__asm__("_ZNK12CycMetaValue13GetTextLengthEv");
/* TncMetaValue */
extern _Bool    TncIsSigned(const void*)     __asm__("_ZNK12TncMetaValue8IsSignedEv");
extern unsigned TncGetTextLength(const void*)__asm__("_ZNK12TncMetaValue13GetTextLengthEv");
extern _Bool    TncGetArraySize(const void*) __asm__("_ZNK12TncMetaValue12GetArraySizeEv");
extern _Bool    TncIsSignedQ(const void*)    __asm__("_ZNK12TncMetaValue9IsSignedQEv");

static int II = 0, DI = 0;
static uint64_t bits(double d){ union { double d; uint64_t u; } x; x.d=d; return x.u; }
static void I(int v){ printf("I %d %d\n", II++, v); }
static void D(double v){ printf("D %d %016llx %.17g\n", DI++, (unsigned long long)bits(v), v); }

int main(void)
{
    /* ---- static methods ---- */
    static const double dv[] = { -3.0, -1.0, -0.5, 0.0, 0.5, 1.0, 1.0000001,
                                 2.0, 25.4, 1e9, -1e9 };
    for (int b = 0; b <= 1; b++)
        for (unsigned k = 0; k < sizeof(dv)/sizeof(dv[0]); k++)
            I(InchPrecision(b, dv[k]));
    for (int m = 0; m <= 1; m++) {
        D(ToMetricFeedValue(m));
        D(ToMetricPosValue(m));
        D(ToNonMetricFeedValue(m));
        D(ToNonMetricPosValue(m));
    }

    /* ---- CycMetaValue: flat byte object ---- */
    static const double sd[] = { -1.0, -0.0, 0.0, 1e-300, -1e-300, 5.0, -5.0, 1e308, -1e308 };
    static const uint32_t tl[] = { 0u, 1u, 7u, 255u, 65535u, 70000u, 0x7fffffffu, 0xffffffffu };
    for (unsigned k = 0; k < sizeof(sd)/sizeof(sd[0]); k++) {
        unsigned char obj[CYC_OBJ_SIZE]; memset(obj, 0xAA, sizeof obj);
        memcpy(obj + CYC_SIGNED_OFF, &sd[k], sizeof(double));
        I(CycIsSigned(obj));
        I(CycIsCardinal(obj));
        I(CycIsInteger(obj));
        I(CycIsReal(obj));
        I(CycGetArraySize(obj));
    }
    for (unsigned k = 0; k < sizeof(tl)/sizeof(tl[0]); k++) {
        unsigned char obj[CYC_OBJ_SIZE]; memset(obj, 0x55, sizeof obj);
        memcpy(obj + CYC_TEXTLEN_OFF, &tl[k], sizeof(uint32_t));
        I((int)CycGetTextLength(obj));
    }

    /* ---- TncMetaValue: pImpl object built per-arch ---- */
    static const uint16_t txt[] = { 0, 1, 42, 255, 256, 65535 };
    static const uint8_t  typ[] = { 0, 5, 6, 7, 0xff };
    static const uint8_t  flg[] = { 0x00, 0x20, 0x21, 0xff, 0xdf };  /* bit5 = 0x20 */
    for (int impl = 0; impl <= 1; impl++) {
        for (unsigned a = 0; a < sizeof(txt)/sizeof(txt[0]); a++)
        for (unsigned b = 0; b < sizeof(typ)/sizeof(typ[0]); b++)
        for (unsigned c = 0; c < sizeof(flg)/sizeof(flg[0]); c++) {
            TncFmt fmt; memset(&fmt, 0, sizeof fmt);
            fmt.textlen = txt[a]; fmt.type = typ[b]; fmt.flags = flg[c];
            TncObj obj; obj.vtable = (void*)0; obj.fmt = &fmt;
            obj.impl = impl ? (void*)&obj : (void*)0;
            I(TncIsSigned(&obj));
            I(TncGetArraySize(&obj));
            I(TncIsSignedQ(&obj));
            I((int)TncGetTextLength(&obj));
        }
    }
    return 0;
}
