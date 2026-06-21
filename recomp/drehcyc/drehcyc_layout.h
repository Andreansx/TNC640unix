/*
 * drehcyc_layout.h — the aufmass_rt struct passed BY VALUE to is_aufmass_aktiv.
 * The i386 by-value layout (from disasm @0xbcb0): flag @+0, m1 @+4, m2 @+0xc
 * (i386 doubles are 4-byte aligned). Natural C layout reproduces this on i386;
 * on ARM64 the doubles are 8-aligned, but the harness and the reimplementation
 * share this definition so each arch's by-value ABI carries the same logical
 * fields.
 */
#ifndef DREHCYC_LAYOUT_H
#define DREHCYC_LAYOUT_H

typedef struct {
    unsigned char flag;   /* +0    : bits 0x30 gate "active" */
    double        m1;     /* i386 +0x4 */
    double        m2;     /* i386 +0xc */
} aufmass_rt;

#endif
