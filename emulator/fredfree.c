/* fredfree.c — i386 LD_PRELOAD: a PROCESS-SCOPED no-op free, active ONLY in Fred (Ed/mmi).
 *
 * DIAGNOSTIC for the Gate-2 Fred crash: Fred (Ed/mmi) SIGSEGVs in libbackend
 * FThread::EvalContextModule (libbackend+0x28bef/+0x28bf2) dereferencing P = this->m_0x60[idx],
 * a FThread *context object* that is a DANGLING pointer — sometimes unmapped (fault reading *P),
 * sometimes readable-but-garbage-vtable (fault at `call *0x18(*P)`), with the garbage VALUE varying
 * every run (0x0 / 0x656d6186 / 0x881c04fe). That signature = a deterministic USE-AFTER-FREE of a
 * context object (freed, then re-evaluated). Reproduced 4x, and it SURVIVES CPU pinning to one core
 * (so it is NOT a parallelism race) — a deterministic lifetime bug.
 *
 * This preload is loaded into EVERY spawned child (they share AppStartMP's LD_PRELOAD), but it
 * SELF-SCOPES: free()/operator delete become a leak-everything no-op ONLY when the process is Ed/mmi
 * (getenv("HEROS_PROC_NAME") contains "mmi" — winmgr/skmgr/prom/evtserver/cfgserver/AppStart do not).
 * Every other process frees normally (forward to __libc_free, no dlsym — FEX-safe). If no-op'ing Fred's
 * frees makes the EvalContextModule crash DISAPPEAR (the freed object stays valid), the crash is proven
 * a use-after-free and Fred advances to its next state — turning a pinned blocker into forward motion.
 * A short-lived leak is harmless for this scouting run. Gated by the run script's FREDFREE=1 knob.
 *
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o fredfree.so emulator/fredfree.c */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>

extern void __libc_free(void *);

static int fred = -1;
static int in_fred(void){
    if(fred < 0){ const char *n = getenv("HEROS_PROC_NAME"); fred = (n && strstr(n, "mmi")) ? 1 : 0; }
    return fred;
}

void free(void *p){ if(in_fred()) return; __libc_free(p); }
void cfree(void *p){ if(in_fred()) return; __libc_free(p); }
/* C++ operator delete family (public libstdc++ symbols; interpose to catch module-object deletes) */
void _ZdlPv(void *p){ if(in_fred()) return; __libc_free(p); }                 /* operator delete(void*) */
void _ZdaPv(void *p){ if(in_fred()) return; __libc_free(p); }                 /* operator delete[](void*) */
void _ZdlPvj(void *p, unsigned s){ (void)s; if(in_fred()) return; __libc_free(p); }   /* sized delete */
void _ZdaPvj(void *p, unsigned s){ (void)s; if(in_fred()) return; __libc_free(p); }
