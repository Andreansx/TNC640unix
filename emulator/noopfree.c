/* noopfree.c — i386 LD_PRELOAD: make application free()/operator delete a NO-OP (leak everything).
 * ConfigServer crashes with "free(): invalid pointer" while deserializing/handling HrMmi's 0x170501
 * CfgGetData (it frees a pointer mis-derived from the message under the FEX-native emulation). For a
 * short-lived test ConfigServer process, leaking is harmless — and skipping the bad free lets it stay
 * alive and SERVE HrMmi's config so HrMmi can progress past its GUI-loop wait. If this makes ConfigServer
 * survive + serve, the bad free is a benign over-free (not a use-after-free), and the fix can be narrowed.
 * glibc-internal frees use __libc_free (not this public symbol), so malloc bookkeeping is untouched.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o noopfree.so emulator/noopfree.c */
#include <stddef.h>

void free(void *p){ (void)p; }
void cfree(void *p){ (void)p; }
/* C++ operator delete family (libstdc++ resolves these internally; interpose to be safe) */
void _ZdlPv(void *p){ (void)p; }                         /* operator delete(void*) */
void _ZdaPv(void *p){ (void)p; }                         /* operator delete[](void*) */
void _ZdlPvj(void *p, unsigned int s){ (void)p; (void)s; }   /* sized delete (32-bit size_t) */
void _ZdaPvj(void *p, unsigned int s){ (void)p; (void)s; }
void _ZdlPvSt11align_val_t(void *p, unsigned int a){ (void)p; (void)a; }
void _ZdlPvjSt11align_val_t(void *p, unsigned int s, unsigned int a){ (void)p; (void)s; (void)a; }
