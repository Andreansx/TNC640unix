/*
 * libEp90_Dm_dkomp_rebuilt.c
 *
 * BEHAVIORAL reimplementation (native ARM64) of the lock-free "huelle"
 * doubly-linked-list navigators of the proprietary i386 libEp90_Dm.so — the
 * `dkomp_nw_get_huelle_*` family. These are genuine MULTI-LEVEL POINTER CHASERS
 * (handle -> slot -> container -> node -> next/prev) with a mutating cursor —
 * exactly the class excluded from the byte-identical bar because a 32-bit
 * stored pointer can't address a 64-bit buffer. We prove OBSERVABLE EQUIVALENCE
 * by driving an equivalent native list and comparing the traversed node
 * *identities* (payload tags) and cursor state, differentially against the real
 * i386 .so under qemu-i386.
 *
 * Source of truth: work/re/out/libEp90_Dm.so.decomp.c (Ghidra).
 */
#include "dkomp_layout.h"

/* resolve handle+channel -> Container* (the (handle->slot)=piVar1, *piVar1) chase */
static Container *resolve(void *handle, unsigned char ch)
{
    if (!handle) return 0;
    Container **pp = ((Handle *)handle)->slots[(unsigned char)(ch - 1)];
    if (!pp) return 0;
    return *pp;   /* iVar2 = *piVar1 */
}

/* dkomp_nw_get_huelle(handle, ch) -> the Container pointer (*piVar1) */
void *dkomp_nw_get_huelle(void *handle, int ch)
{
    if (!handle) return 0;
    Container **pp = ((Handle *)handle)->slots[(unsigned char)(ch - 1)];
    if (!pp) return 0;
    return *pp;
}

/* first: cursor = head; return head */
void *dkomp_nw_get_huelle_first(void *handle, unsigned char ch)
{
    Container *c = resolve(handle, ch);
    if (!c) return 0;
    c->cursor = c->head;
    return c->head;
}

/* last: cursor = tail; return tail */
void *dkomp_nw_get_huelle_last(void *handle, unsigned char ch)
{
    Container *c = resolve(handle, ch);
    if (!c) return 0;
    c->cursor = c->tail;
    return c->tail;
}

/* current: return cursor */
void *dkomp_nw_get_huelle_current(void *handle, unsigned char ch)
{
    Container *c = resolve(handle, ch);
    if (!c) return 0;
    return c->cursor;
}

/* next: if cursor->next, advance cursor and return it; else 0 (cursor unchanged) */
void *dkomp_nw_get_huelle_next(void *handle, unsigned char ch)
{
    Container *c = resolve(handle, ch);
    if (!c) return 0;
    Node *nx = c->cursor->next;
    if (nx) { c->cursor = nx; return nx; }
    return 0;
}

/* prev: if cursor->prev, retreat cursor and return it; else 0 (cursor unchanged) */
void *dkomp_nw_get_huelle_prev(void *handle, unsigned char ch)
{
    Container *c = resolve(handle, ch);
    if (!c) return 0;
    Node *pv = c->cursor->prev;
    if (pv) { c->cursor = pv; return pv; }
    return 0;
}

/* ---- "hilf" family: same container/node layout, different handle->container
 *      resolution (handle slot -> wrapper, wrapper+4 -> container). ---- */
static Container *resolve_hilf(void *handle, unsigned char ch)
{
    if (!handle) return 0;
    HilfWrap *w = ((HilfHandle *)handle)->slots[(unsigned char)(ch - 1)];
    if (!w) return 0;
    return w->container;
}

void *dkomp_nw_get_hilf_first(void *handle, unsigned char ch)
{
    Container *c = resolve_hilf(handle, ch);
    if (!c) return 0;
    c->cursor = c->head;
    return c->head;
}

void *dkomp_nw_get_hilf_last(void *handle, unsigned char ch)
{
    Container *c = resolve_hilf(handle, ch);
    if (!c) return 0;
    c->cursor = c->tail;
    return c->tail;
}

void *dkomp_nw_get_hilf_current(void *handle, unsigned char ch)
{
    Container *c = resolve_hilf(handle, ch);
    if (!c) return 0;
    return c->cursor;
}

void *dkomp_nw_get_hilf_next(void *handle, unsigned char ch)
{
    Container *c = resolve_hilf(handle, ch);
    if (!c) return 0;
    Node *nx = c->cursor->next;
    if (nx) { c->cursor = nx; return nx; }
    return 0;
}

void *dkomp_nw_get_hilf_prev(void *handle, unsigned char ch)
{
    Container *c = resolve_hilf(handle, ch);
    if (!c) return 0;
    Node *pv = c->cursor->prev;
    if (pv) { c->cursor = pv; return pv; }
    return 0;
}

/* ---- "edge" family: caller-supplied cursor; descriptor holds start/end ---- */

/* start: cursor = desc->start; return it */
void *dkomp_nw_get_edge_start(void *handle, unsigned char ch)
{
    if (!handle) return 0;
    EdgeDesc *d = ((EdgeHandle *)handle)->slots[(unsigned char)(ch - 1)];
    if (!d) return 0;
    Node *s = d->start;
    d->container->cursor = s;
    return s;
}

/* end: cursor = desc->end; return it */
void *dkomp_nw_get_edge_end(void *handle, unsigned char ch)
{
    if (!handle) return 0;
    EdgeDesc *d = ((EdgeHandle *)handle)->slots[(unsigned char)(ch - 1)];
    if (!d) return 0;
    Node *e = d->end;
    d->container->cursor = e;
    return e;
}

/* next: if node != 0 and node != end, return node->next (and set cursor) */
void *dkomp_nw_get_edge_next(void *handle, int ch, void *node)
{
    if (!node) return 0;
    EdgeDesc *d = ((EdgeHandle *)handle)->slots[(unsigned char)(ch - 1)];
    if (d->end == node) return 0;
    Node *nx = ((Node *)node)->next;
    d->container->cursor = nx;
    return nx;
}

/* prev: if node != 0 and node != start, return node->prev (and set cursor) */
void *dkomp_nw_get_edge_prev(void *handle, int ch, void *node)
{
    if (!node) return 0;
    EdgeDesc *d = ((EdgeHandle *)handle)->slots[(unsigned char)(ch - 1)];
    if (d->start == node) return 0;
    Node *pv = ((Node *)node)->prev;
    d->container->cursor = pv;
    return pv;
}

/* ---- rot3D / box3D: the hilf pattern with a different container offset
 *      (rot3D @+0x18, box3D @+0x1c). The five-navigator bodies are identical to
 *      hilf's, so a macro keeps them in lockstep. ---- */
static Container *resolve_rot3D(void *handle, unsigned char ch)
{
    if (!handle) return 0;
    Rot3DWrap *w = ((Rot3DHandle *)handle)->slots[(unsigned char)(ch - 1)];
    return w ? w->container : 0;
}
static Container *resolve_box3D(void *handle, unsigned char ch)
{
    if (!handle) return 0;
    Box3DWrap *w = ((Box3DHandle *)handle)->slots[(unsigned char)(ch - 1)];
    return w ? w->container : 0;
}

#define DKOMP_NAV_FAMILY(fam, RESOLVE)                                          \
void *dkomp_nw_get_##fam##_first(void *h, unsigned char ch)                     \
{ Container *c = RESOLVE(h, ch); if (!c) return 0; c->cursor = c->head; return c->head; } \
void *dkomp_nw_get_##fam##_last(void *h, unsigned char ch)                      \
{ Container *c = RESOLVE(h, ch); if (!c) return 0; c->cursor = c->tail; return c->tail; } \
void *dkomp_nw_get_##fam##_current(void *h, unsigned char ch)                   \
{ Container *c = RESOLVE(h, ch); if (!c) return 0; return c->cursor; }          \
void *dkomp_nw_get_##fam##_next(void *h, unsigned char ch)                      \
{ Container *c = RESOLVE(h, ch); if (!c) return 0; Node *n = c->cursor->next; if (n) { c->cursor = n; return n; } return 0; } \
void *dkomp_nw_get_##fam##_prev(void *h, unsigned char ch)                      \
{ Container *c = RESOLVE(h, ch); if (!c) return 0; Node *n = c->cursor->prev; if (n) { c->cursor = n; return n; } return 0; }

DKOMP_NAV_FAMILY(rot3D, resolve_rot3D)
DKOMP_NAV_FAMILY(box3D, resolve_box3D)
