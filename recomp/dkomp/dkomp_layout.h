/*
 * dkomp_layout.h — object layout for the libEp90_Dm "huelle" doubly-linked
 * list, shared by the reimplementation and the harness. As with metaval, the
 * structs mirror the original FIELD ORDER so the SAME source reproduces the
 * i386 byte offsets when compiled for i386 (4-byte pointers) and lays out
 * native 8-byte pointers when compiled for ARM64. Both the real navigator and
 * the reimplementation then chase their own arch's correct offsets.
 *
 * From the decompile (dkomp_nw_get_huelle_*):
 *   handle: slot array at byte 0x2c, stride = sizeof(ptr); slot holds a
 *           Container** (piVar1); *piVar1 = the Container.
 *   container: head @0x20, tail @0x24(i386), cursor @0x28(i386).
 *   node: next @0, prev @4(i386).  (+ a payload tag we add for identity)
 */
#ifndef DKOMP_LAYOUT_H
#define DKOMP_LAYOUT_H

typedef struct Node {
    struct Node *next;   /* +0 */
    struct Node *prev;   /* i386:+4 / arm:+8 */
    int          tag;    /* payload for cross-arch identity (not read by lib) */
} Node;

typedef struct {
    char  pad[0x20];
    Node *head;          /* +0x20 */
    Node *tail;          /* i386:+0x24 */
    Node *cursor;        /* i386:+0x28 */
} Container;

#define DKOMP_NCH 4
typedef struct {
    char        pad[0x2c];
    Container **slots[DKOMP_NCH];   /* slot[ch-1] = &(Container* holder) */
} Handle;

/* The "hilf" family uses a different indirection: handle slot -> wrapper, then
 * wrapper+4(i386) -> container. (huelle: slot -> piVar1, *piVar1 -> container.) */
typedef struct {
    char       pad[4];
    Container *container;   /* i386:+4 / arm:+8 */
} HilfWrap;

typedef struct {
    char       pad[0x2c];
    HilfWrap  *slots[DKOMP_NCH];     /* slot[ch-1] = &wrapper */
} HilfHandle;

/* The "edge" family: the slot value is a descriptor whose word [0] is the
 * container, [2] the start node and [3] the end node. next/prev take the
 * current node EXPLICITLY (caller-supplied cursor) rather than the stored one. */
typedef struct {
    Container *container;   /* [0]  cursor lives at container+0x28        */
    void      *field1;      /* [1]                                        */
    Node      *start;       /* [2]                                        */
    Node      *end;         /* [3]                                        */
} EdgeDesc;

typedef struct {
    char       pad[0x2c];
    EdgeDesc  *slots[DKOMP_NCH];     /* slot[ch-1] = &descriptor */
} EdgeHandle;

/* rot3D and box3D are the hilf pattern with a different container offset inside
 * the channel descriptor: hilf@+4, rot3D@+0x18, box3D@+0x1c. (All four families
 * share one channel descriptor in the real control; we model each separately.) */
typedef struct { char pad[0x18]; Container *container; } Rot3DWrap;   /* +0x18 */
typedef struct { char pad[0x1c]; Container *container; } Box3DWrap;   /* +0x1c */

typedef struct { char pad[0x2c]; Rot3DWrap *slots[DKOMP_NCH]; } Rot3DHandle;
typedef struct { char pad[0x2c]; Box3DWrap *slots[DKOMP_NCH]; } Box3DHandle;

#endif
