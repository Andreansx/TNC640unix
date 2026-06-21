/*
 * libwinmgrlib_partial_rebuilt.c — native re-implementation of the PURE-INTEGER
 * single-level leaf accessors of the proprietary i386 libwinmgrlib.so (HeROS
 * window manager), recovered via i386 disassembly.
 *
 * SCOPE: most of libwinmgrlib drives X11 (libX11/libXext) and the HeROS message
 * bus — NOT leaves. This file reproduces the small EXPORTED window-handle
 * accessors that touch only single struct fields of a caller buffer. cdecl.
 */
#include <stdint.h>

/* CheckWindow(unused, win, out): if (*win==0 && *(win+8)==*out) *(out+4)=*(win+0xc);
 * always returns 0. (arg1 at [esp+4] is unused; win=[esp+8], out=[esp+0xc].) */
int CheckWindow(int unused, void *win, void *out)
{
    (void)unused;
    uint32_t *w = (uint32_t *)win;
    uint32_t *o = (uint32_t *)out;
    if (w[0] == 0 && w[2] == o[0])     /* *(win+0)==0 && *(win+8)==*(out+0) */
        o[1] = w[3];                   /* *(out+4) = *(win+0xc) */
    return 0;
}

/* p ? *(uint32*)(p+0x4c) : 0 */
unsigned WmGetMessageCount(void *p)
{
    return p ? *(uint32_t *)((char *)p + 0x4c) : 0u;
}

/* *(uint32*)(p+0x38)  (no null check in the original) */
unsigned WmMustConfirmEvent(void *p)
{
    return *(uint32_t *)((char *)p + 0x38);
}

/* ++*(uint32*)(p+0x44), returns the incremented value */
unsigned AllocWindow(void *p)
{
    uint32_t *c = (uint32_t *)((char *)p + 0x44);
    return ++(*c);
}

/* read-and-clear: v=*(p+0x3c); *(p+0x3c)=0; return v */
unsigned WmGetLastError(void *p)
{
    uint32_t *e = (uint32_t *)((char *)p + 0x3c);
    uint32_t v = *e;
    *e = 0;
    return v;
}

/* ret (no body); returns nothing meaningful */
void FreeWindow(void *p)
{
    (void)p;
}
