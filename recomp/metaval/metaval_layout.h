/*
 * metaval_layout.h — object layout shared by the reimplementation and the
 * verification harness for libtncMetaValue's leaf class methods.
 *
 * Key idea for the `this`-based methods: we mirror the i386 class FIELD ORDER
 * (not raw byte offsets). The SAME source compiled for i386 reproduces the
 * original byte offsets (4-byte pointers -> fmt@+4, impl@+8), and compiled for
 * ARM64 lays out arch-native 8-byte pointers. Both the real i386 method and the
 * native reimplementation then read their own arch's correct offsets, so a
 * single harness drives both with identical *logical* inputs.
 */
#ifndef METAVAL_LAYOUT_H
#define METAVAL_LAYOUT_H
#include <stdint.h>

/* ---- CycMetaValue: fields live INSIDE the object (no pointer indirection),
 *      so a flat byte buffer works identically on both arches. ----
 *   IsSigned:      *(double*)(this + 0x0c) < 0
 *   GetTextLength: *(uint32*)(this + 0x24)
 */
#define CYC_OBJ_SIZE      0x28
#define CYC_SIGNED_OFF    0x0c   /* double  */
#define CYC_TEXTLEN_OFF   0x24   /* uint32  */

/* ---- TncMetaValue: a pImpl object. 'fmt' (format descriptor) and 'impl'
 *      (value implementation) are pointers; methods chase them. ---- */
typedef struct {
    uint16_t textlen;          /* +0x00  GetTextLength returns this           */
    uint8_t  type;             /* +0x02  IsSignedQ checks == 6                */
    uint8_t  _pad[0x10 - 3];
    uint8_t  flags;            /* +0x10  bit5 = signed                        */
} TncFmt;

typedef struct {
    void   *vtable;            /* +0           (unused by these leaves)        */
    TncFmt *fmt;               /* i386:+0x04 / arm:+0x08                       */
    void   *impl;              /* i386:+0x08 / arm:+0x10                       */
} TncObj;

#endif
