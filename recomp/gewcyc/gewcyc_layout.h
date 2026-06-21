/*
 * gewcyc_layout.h — geotec layout for GCYC_Geostart/GCYC_Geoziel, shared by the
 * reimplementation and harness. Mirrors the i386 field offsets so the SAME
 * source reproduces them on i386 (pointer @+0x14 is 4 bytes; doubles at the real
 * byte offsets) and lays out native 8-byte pointers on ARM64. Both the real
 * method and the reimplementation read their own arch's correct layout.
 *
 *   geotec: +0x14 -> sub (direction descriptor), +0x24 startX, +0x2c startY,
 *           +0x90 endX, +0x98 endY
 *   sub:    +0x7b direction flag byte (1 => reversed)
 */
#ifndef GEWCYC_LAYOUT_H
#define GEWCYC_LAYOUT_H

typedef struct { unsigned char pad[0x7b]; unsigned char flag; } GcSub;  /* flag @ +0x7b */

/* the coordinate descriptor reached via *(geotec+0x10): alt-coords gated by a
 * flags word; used by GCYC_SetInkSteigung (+0x60) and GCYC_LeseAltko. */
typedef struct {
    unsigned char p0[0x58];
    double         d58;                /* +0x58 */
    double         d60;                /* +0x60 (incremental pitch) */
    unsigned char  p1[0x80 - 0x68];
    double         d80;                /* +0x80 */
    unsigned char  p2[0xa0 - 0x88];
    unsigned short flags;             /* +0xa0 (bits 2/4/0x40 gate the coords) */
} GcSub2;

typedef struct {
    unsigned char pad0[0x10];
    GcSub2       *sub2;                /* +0x10 */
    GcSub        *sub;                 /* +0x14 */
    unsigned char pad1[0x24 - 0x14 - sizeof(void *)];
    double        sx;                  /* +0x24 (i386) */
    double        sy;                  /* +0x2c (i386) */
    unsigned char pad2[0x90 - 0x2c - sizeof(double)];
    double        ex;                  /* +0x90 (i386) */
    double        ey;                  /* +0x98 (i386) */
} GcGeotec;

/* span node for GCYC_Hole_Spandaten: a singly-linked list (next @+4) of chip
 * records; the function walks to the index-th node and reads 5 doubles. */
typedef struct Span {
    unsigned char p0[4];
    struct Span  *next;                /* +0x4 */
    double        d8;                  /* +0x8  */
    double        d10;                 /* +0x10 */
    double        d18;                 /* +0x18 */
    double        d20;                 /* +0x20 */
    unsigned char p1[0x38 - 0x20 - sizeof(double)];
    double        d38;                 /* +0x38 */
} Span;

#endif
