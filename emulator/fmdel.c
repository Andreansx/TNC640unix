/* fmdel.c — i386 LD_PRELOAD: MINIMAL-footprint free-site capture for the Fred UAF, scoped to Ed/mmi.
 *
 * The freed object at the crash (P = this->m_0x60[0]) is a FrameModule allocated on Fred's main heap
 * (P observed 0xaac0ceb2 / 0xaadc2ec2 — the 0xaa______ region where Fred.elf + its heap map). The earlier
 * 128KB-ring logger (fredfree FREDFREE=2) PERTURBED the layout-sensitive UAF; a size-0xa0 sized-delete filter
 * (v1 of this file) reproduced the crash cleanly but caught NOTHING (the module is freed via UNSIZED delete or
 * plain free, not C++14 sized delete). So this version interposes the WHOLE free/delete family and logs ONE
 * stderr line per call whose pointer lands in the 0xaa______ heap window (cheap mask, low volume, no ring/no
 * atomics -> no layout perturbation). At the crash, heros_rtos prints P; grep "[fmdel] <P>" -> the caller
 * return-address that freed the module = the UAF free SITE.
 *
 * Scoped to Fred (getenv("HEROS_PROC_NAME") ~ "mmi"); other procs forward unchanged. Gated FMDEL=1.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o fmdel.so emulator/fmdel.c */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void __libc_free(void *);

static int fred = -1;
static int in_fred(void){
    if(fred < 0){ const char *n = getenv("HEROS_PROC_NAME"); fred = (n && strstr(n, "mmi")) ? 1 : 0; }
    return fred;
}

/* The CORRUPT object is the m_0x60 vector's BACKING ARRAY (run C: arr=0xdaa02400, arr[1]=0x388cd904 garbage),
 * not the module. std::vector frees its old array via operator delete on realloc. Watch BOTH the vector-array
 * arena (0xda______) and Fred's main heap (0xaa______) so we catch whichever is freed-then-reused. */
static inline int interesting(void *p){
    unsigned long h = (unsigned long)p & 0xff000000ul;
    return h == 0xaa000000ul || h == 0xda000000ul || h == 0xdb000000ul;
}

/* async-signal-safe: "[fmdel] <ptr> <caller>\n" straight to fd 2 */
static void logdel(void *p, void *ra){
    char b[40]; int i = 0; const char *t = "[fmdel] ";
    while(t[i]){ b[i] = t[i]; i++; }
    unsigned long v = (unsigned long)p;
    for(int k = 7; k >= 0; k--){ int d = (v >> (k*4)) & 0xf; b[i++] = d < 10 ? '0'+d : 'a'+d-10; }
    b[i++] = ' ';
    v = (unsigned long)ra;
    for(int k = 7; k >= 0; k--){ int d = (v >> (k*4)) & 0xf; b[i++] = d < 10 ? '0'+d : 'a'+d-10; }
    b[i++] = '\n';
    (void)write(2, b, i);
}

/* NB: raw free() NOT interposed — forwarding it stalled Fred's boot (run I hung at ~101 lines). The corrupt
 * object is the std::vector backing array, freed via operator delete (below), so the delete family suffices. */
void _ZdlPv(void *p){                              /* operator delete(void*) */
    if(p && in_fred() && interesting(p)) logdel(p, __builtin_return_address(0));
    __libc_free(p);
}
void _ZdaPv(void *p){                              /* operator delete[](void*) */
    if(p && in_fred() && interesting(p)) logdel(p, __builtin_return_address(0));
    __libc_free(p);
}
void _ZdlPvj(void *p, unsigned s){ (void)s;         /* sized delete */
    if(p && in_fred() && interesting(p)) logdel(p, __builtin_return_address(0));
    __libc_free(p);
}
void _ZdaPvj(void *p, unsigned s){ (void)s;
    if(p && in_fred() && interesting(p)) logdel(p, __builtin_return_address(0));
    __libc_free(p);
}
