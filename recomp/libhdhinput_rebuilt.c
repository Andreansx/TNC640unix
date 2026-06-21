/*
 * libhdhinput_rebuilt.c
 *
 * Native re-implementation of the pure numeric-field accessor/validator
 * functions from HEIDENHAIN's proprietary i386 `libhdhinput.so`, recovered by
 * Ghidra decompilation (work/re/out/libhdhinput.decomp.c) and rewritten as
 * portable C so it COMPILES TO NATIVE Apple-Silicon ARM64 machine code.
 *
 * Every byte offset, bit mask and branch below is preserved verbatim from the
 * decompiled i386 logic, so the ARM64 build is behaviourally identical to the
 * original (verified byte-for-byte by recomp/verify_harness.c against the real
 * i386 .so running under translation).
 *
 * Scope note: these are the *leaf* functions — pure computation over a
 * "number-type descriptor" struct, no C++ classes/vtables/global state. This is
 * the subset for which decompile->recompile yields correct native code. It does
 * NOT extend to the C++ product as a whole (see docs/16).
 *
 * The descriptor `d` is an opaque byte array; field meanings (from the offsets):
 *   d[2]      = type code (0..8)
 *   d[4..15]  = type-specific payload (min/max as 2x int32 at d+4,d+8; etc.)
 *   d[12..15] = vkomma/nkomma digit counts (offsets 0xc..0xf)
 *   d[16]     = flag byte (0x10): bit6=export 0x40, bit5=sign 0x20, bit7=igsign
 *               0x80, bit4=mm/inch 0x10, bit3=feed 0x08
 */
#include <stdint.h>
#include <string.h>

typedef unsigned char u8;

/* The "type is an active numeric type" predicate shared by several accessors:
 * true for type in {1,2,3,4,5,6} or {8}; false for 0 and 7 (and >8). */
static inline int type_active(u8 t) { return (t < 7) ? (t != 0) : (t == 8); }

u8 get_pzt_export(const u8 *d) { return type_active(d[2]) ? (d[16] & 0x40) : 0; }
u8 get_pzt_vorz  (const u8 *d) { return type_active(d[2]) ? (d[16] & 0x20) : 0; }
u8 get_pzt_igsign(const u8 *d) { return type_active(d[2]) ? (d[16] & 0x80) : 0; }

u8 get_pzt_feed(const u8 *d) {
    return ((u8)(d[2] - 1) < 5) ? ((d[16] >> 3) & 1) : 0;
}
u8 get_pzt_mm_inch(const u8 *d) {
    return (1 < (u8)(d[2] - 2)) ? 0 : (d[16] & 0x10);
}
u8 get_pzt_hex  (const u8 *d) { return d[2] == 1 || d[2] == 4; }
u8 get_pzt_float(const u8 *d) { return d[2] == 3; }

u8 get_pzt_nkomma(int mode, const u8 *d) {
    if (!type_active(d[2])) return 0;
    return mode == 0 ? d[0x0f] : d[0x0e];
}
u8 get_pzt_vkomma(int mode, const u8 *d) {
    if (!type_active(d[2])) return 0;
    return mode == 0 ? d[0x0d] : d[0x0c];
}

/* returns pointer to d+4 when type==0, else NULL */
const u8 *get_pzt_pstr(const u8 *d) { return d[2] != 0 ? 0 : d + 4; }

void get_pzt_perm(const u8 *d, uint32_t out[3]) {
    u8 t = d[2];
    if (t < 7) {
        if (t == 0) { memmove(out, d + 4, 12); return; }      /* copy payload */
    } else if (t != 8) {
        out[0] = out[1] = out[2] = 0; return;                 /* 7 or >8 */
    }
    out[0] = out[1] = out[2] = 0xffffffffu;                   /* 1..6 or 8 */
}

/* regparm3 internal helper in the original; reconstructed with explicit args */
static int CheckMinMax(uint32_t value, const u8 *d) {
    int32_t lo = (int32_t)(d[8]  | (d[9]<<8)  | (d[10]<<16) | ((uint32_t)d[11]<<24));
    int32_t hi = (int32_t)(d[4]  | (d[5]<<8)  | (d[6]<<16)  | ((uint32_t)d[7]<<24));
    if (get_pzt_vorz(d) == 0)                                  /* unsigned range */
        return value <= (uint32_t)hi && (uint32_t)lo <= value;
    return (int32_t)value <= hi && lo <= (int32_t)value;       /* signed range */
}

/* type 3 pre-transform: clear bit 30 of the magnitude (sign-preserving).
 * Mirrors the i386 `and ecx,0xbfffffff` around a neg/neg of the value. */
static uint32_t xf_clear_bit30(uint32_t v) {
    if (!(v & 0x80000000u)) return v & 0xBFFFFFFFu;
    uint32_t mag = 0u - v;                 /* unsigned negate == i386 `neg` */
    mag &= 0xBFFFFFFFu;
    return 0u - mag;
}
/* type 6 pre-transform: +1 if negative (matches `cmp 0x80000000; sbb -1`). */
static uint32_t xf_inc_if_neg(uint32_t v) { return (v & 0x80000000u) ? v + 1u : v; }

unsigned check_pzt_range(uint32_t value, const u8 *d) {
    switch (d[2]) {
    case 0: case 7:                       return 0;
    case 1: case 2: case 4: case 5:       return CheckMinMax(value, d);
    case 3:                               return CheckMinMax(xf_clear_bit30(value), d);
    case 6:                               return CheckMinMax(xf_inc_if_neg(value), d);
    case 8:                               return value == 0xff || (value + 1u) < 2u;
    default:                              return 1;
    }
}

/* membership test of `ch` in a 96-bit charset bitmap (chars 0x20..0x7f) */
int check_zt_char(const u8 *bitmap, char ch) {
    unsigned u = (unsigned)((int)ch - 0x20);
    if (bitmap != 0 && u < 0x60)
        return ((0x80 >> (u & 7)) & bitmap[u >> 3]) != 0;
    return 0;
}
