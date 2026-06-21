/*
 * verify_dkomp.c — one harness, compiled two ways (i386 vs the real .so under
 * qemu; arm64 vs the reimplementation). Builds an equivalent doubly-linked list
 * per-arch and drives the cursor through a fixed navigation sequence. Because
 * the returned values are NODE POINTERS (different widths per arch), we never
 * compare them raw — we deref each and print the node's payload TAG, which is
 * the same logical value on both sides. Output is deterministic → proof is diff.
 */
#include <stdio.h>
#include <string.h>
#include "dkomp_layout.h"

extern void *dkomp_nw_get_huelle(void *, int);
extern void *dkomp_nw_get_huelle_first(void *, unsigned char);
extern void *dkomp_nw_get_huelle_last(void *, unsigned char);
extern void *dkomp_nw_get_huelle_current(void *, unsigned char);
extern void *dkomp_nw_get_huelle_next(void *, unsigned char);
extern void *dkomp_nw_get_huelle_prev(void *, unsigned char);
extern void *dkomp_nw_get_hilf_first(void *, unsigned char);
extern void *dkomp_nw_get_hilf_last(void *, unsigned char);
extern void *dkomp_nw_get_hilf_current(void *, unsigned char);
extern void *dkomp_nw_get_hilf_next(void *, unsigned char);
extern void *dkomp_nw_get_hilf_prev(void *, unsigned char);
extern void *dkomp_nw_get_edge_start(void *, unsigned char);
extern void *dkomp_nw_get_edge_end(void *, unsigned char);
extern void *dkomp_nw_get_edge_next(void *, int, void *);
extern void *dkomp_nw_get_edge_prev(void *, int, void *);
/* rot3D/box3D export first/last/next/prev (NO _current, unlike huelle/hilf) */
extern void *dkomp_nw_get_rot3D_first(void *, unsigned char);
extern void *dkomp_nw_get_rot3D_last(void *, unsigned char);
extern void *dkomp_nw_get_rot3D_next(void *, unsigned char);
extern void *dkomp_nw_get_rot3D_prev(void *, unsigned char);
extern void *dkomp_nw_get_box3D_first(void *, unsigned char);
extern void *dkomp_nw_get_box3D_last(void *, unsigned char);
extern void *dkomp_nw_get_box3D_next(void *, unsigned char);
extern void *dkomp_nw_get_box3D_prev(void *, unsigned char);

static void emit(const char *op, void *p) { printf("%s %d\n", op, p ? ((Node *)p)->tag : 0); }

/* build a handle whose channel `ch` points (via the piVar1 double-indirection)
 * at a container over `n` nodes with tags 10,20,...; cursor starts at head. */
static Handle H;
static Container C;
static Container *Cptr;
static Node NODES[8];

static void build(int n)
{
    memset(&H, 0, sizeof H);
    memset(&C, 0, sizeof C);
    memset(NODES, 0, sizeof NODES);
    for (int i = 0; i < n; i++) {
        NODES[i].tag = (i + 1) * 10;
        NODES[i].next = (i + 1 < n) ? &NODES[i + 1] : 0;
        NODES[i].prev = (i > 0) ? &NODES[i - 1] : 0;
    }
    C.head   = n ? &NODES[0] : 0;
    C.tail   = n ? &NODES[n - 1] : 0;
    C.cursor = C.head;
    Cptr = &C;
    H.slots[0] = &Cptr;   /* piVar1 = &Cptr ; *piVar1 = &C */
}

int main(void)
{
    /* ---- 5-node list, full forward/backward sweep exercising the cursor ---- */
    build(5);
    emit("base_nonnull", dkomp_nw_get_huelle(&H, 1) ? (void *)&NODES[0] : 0);
    emit("first",   dkomp_nw_get_huelle_first(&H, 1));
    for (int i = 0; i < 6; i++) emit("next", dkomp_nw_get_huelle_next(&H, 1));   /* 20,30,40,50,0,0 */
    emit("current", dkomp_nw_get_huelle_current(&H, 1));                          /* 50 */
    for (int i = 0; i < 6; i++) emit("prev", dkomp_nw_get_huelle_prev(&H, 1));   /* 40,30,20,10,0,0 */
    emit("current", dkomp_nw_get_huelle_current(&H, 1));                          /* 10 */
    emit("last",    dkomp_nw_get_huelle_last(&H, 1));                             /* 50 */
    emit("prev",    dkomp_nw_get_huelle_prev(&H, 1));                             /* 40 */
    emit("first",   dkomp_nw_get_huelle_first(&H, 1));                            /* 10 */

    /* ---- 1-node list ---- */
    build(1);
    emit("first",   dkomp_nw_get_huelle_first(&H, 1));                            /* 10 */
    emit("next",    dkomp_nw_get_huelle_next(&H, 1));                             /* 0  */
    emit("prev",    dkomp_nw_get_huelle_prev(&H, 1));                             /* 0  */
    emit("last",    dkomp_nw_get_huelle_last(&H, 1));                             /* 10 */

    /* ---- empty container: only the non-deref ops are defined ---- */
    build(0);
    emit("first",   dkomp_nw_get_huelle_first(&H, 1));                            /* 0 */
    emit("last",    dkomp_nw_get_huelle_last(&H, 1));                             /* 0 */
    emit("current", dkomp_nw_get_huelle_current(&H, 1));                          /* 0 */

    /* ---- null handle / empty slot guards ---- */
    emit("nullhandle_first", dkomp_nw_get_huelle_first(0, 1));                    /* 0 */
    emit("nullhandle_curr",  dkomp_nw_get_huelle_current(0, 1));                  /* 0 */
    build(5);
    H.slots[0] = 0;  /* empty slot */
    emit("nullslot_first",   dkomp_nw_get_huelle_first(&H, 1));                   /* 0 */

    /* ---- hilf family: same list content, the wrapper-indirection handle ---- */
    build(5);
    HilfHandle HH; HilfWrap HW;
    memset(&HH, 0, sizeof HH); memset(&HW, 0, sizeof HW);
    HW.container = &C; HH.slots[0] = &HW;
    emit("h_first",   dkomp_nw_get_hilf_first(&HH, 1));                           /* 10 */
    for (int i = 0; i < 6; i++) emit("h_next", dkomp_nw_get_hilf_next(&HH, 1));   /* 20..50,0,0 */
    emit("h_current", dkomp_nw_get_hilf_current(&HH, 1));                         /* 50 */
    for (int i = 0; i < 6; i++) emit("h_prev", dkomp_nw_get_hilf_prev(&HH, 1));   /* 40..10,0,0 */
    emit("h_last",    dkomp_nw_get_hilf_last(&HH, 1));                            /* 50 */
    HH.slots[0] = 0;
    emit("h_nullslot", dkomp_nw_get_hilf_first(&HH, 1));                          /* 0 */

    /* ---- edge family: descriptor with start/end, caller-supplied cursor ---- */
    build(5);
    EdgeHandle EH; EdgeDesc ED;
    memset(&EH, 0, sizeof EH); memset(&ED, 0, sizeof ED);
    ED.container = &C; ED.start = &NODES[0]; ED.end = &NODES[4];
    EH.slots[0] = &ED;
    void *cur = dkomp_nw_get_edge_start(&EH, 1); emit("e_start", cur);            /* 10 */
    for (int i = 0; i < 6; i++) { cur = dkomp_nw_get_edge_next(&EH, 1, cur); emit("e_next", cur); }
    void *c2 = dkomp_nw_get_edge_end(&EH, 1); emit("e_end", c2);                  /* 50 */
    for (int i = 0; i < 6; i++) { c2 = dkomp_nw_get_edge_prev(&EH, 1, c2); emit("e_prev", c2); }
    EH.slots[0] = 0;
    emit("e_nullslot", dkomp_nw_get_edge_start(&EH, 1));                          /* 0 */

    /* ---- rot3D family (hilf pattern, container @ +0x18) ---- */
    build(5);
    Rot3DHandle RH; Rot3DWrap RW;
    memset(&RH, 0, sizeof RH); memset(&RW, 0, sizeof RW);
    RW.container = &C; RH.slots[0] = &RW;
    emit("r_first",   dkomp_nw_get_rot3D_first(&RH, 1));                          /* 10 */
    for (int i = 0; i < 6; i++) emit("r_next", dkomp_nw_get_rot3D_next(&RH, 1));
    for (int i = 0; i < 6; i++) emit("r_prev", dkomp_nw_get_rot3D_prev(&RH, 1));
    emit("r_last",    dkomp_nw_get_rot3D_last(&RH, 1));                           /* 50 */
    RH.slots[0] = 0;
    emit("r_nullslot", dkomp_nw_get_rot3D_first(&RH, 1));                         /* 0 */

    /* ---- box3D family (hilf pattern, container @ +0x1c) ---- */
    build(5);
    Box3DHandle BH; Box3DWrap BW;
    memset(&BH, 0, sizeof BH); memset(&BW, 0, sizeof BW);
    BW.container = &C; BH.slots[0] = &BW;
    emit("b_first",   dkomp_nw_get_box3D_first(&BH, 1));                          /* 10 */
    for (int i = 0; i < 6; i++) emit("b_next", dkomp_nw_get_box3D_next(&BH, 1));
    for (int i = 0; i < 6; i++) emit("b_prev", dkomp_nw_get_box3D_prev(&BH, 1));
    emit("b_last",    dkomp_nw_get_box3D_last(&BH, 1));                           /* 50 */
    BH.slots[0] = 0;
    emit("b_nullslot", dkomp_nw_get_box3D_first(&BH, 1));                         /* 0 */
    return 0;
}
