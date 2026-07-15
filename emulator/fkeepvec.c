/* fkeepvec.c — i386 LD_PRELOAD: reproduce the REFERENCE allocator's lazy-reuse for ONE freed block, scoped to Fred.
 *
 * GROUND TRUTH (this session): the Fred SIGSEGV is a use-after-free INSIDE libbackend, not an emulator misroute.
 *   - Crash = FThread::EvalContextModule (libbackend+0x28bf2): reads P = this->m_0x60[R->idx] then *P then
 *     call *(*P+0x18). At the fault this->m_0x60 (the vector<FStartable*> backing array, obs 0xdaa02400) holds
 *     GARBAGE (arr[0]=0xaac10ec2, misaligned) -> *P garbage vtable -> fault addr = *P+0x18 (obs 0x05b306da).
 *   - The backing array 0xdaa02400 was FREED by FThread::~FThread's `operator delete(m_0x60)` at libbackend+0x26ebe
 *     (return address libbackend+0x26ec3; pinned un-fakeably: fmdel logged this free with caller f66ebec3 =
 *     0xf66c5000 base + 0x26ec3). ~FThread took the +0x78==0 (self-terminate) path, so it fully frees.
 *   - Same thread (t10e): no cross-thread race, no queue I/O in the crash window. A virtual call within the
 *     context-eval loop self-destructs the FThread (frees its module vector); the emulator's malloc IMMEDIATELY
 *     reuses the 0xda block (a burst of libfrontend frees/allocs overwrites it) -> the next EvalContextModule
 *     reads the clobbered vector. On the real control the same vendor-buggy code survives because native glibc
 *     does NOT hand that block back out inside the same window (lazy reuse) -> arr[0] still points at the intact
 *     module, whose vtable is valid, so the virtual call works.
 *
 * FIX (environmental, NOT an inject): make the emulator match the reference allocator for EXACTLY this one block.
 * Interpose operator delete; when the caller return address == libbackend+0x26ec3 (the ~FThread module-vector
 * delete, and ONLY that site), LEAK the block instead of freeing it. This preserves arr[] contents so the genuine
 * EvalContextModule reads valid module pointers -- the identical code path that runs on real hardware. It does not
 * skip the crash instruction, synthesize any state, or alter control flow; it only withholds one block from reuse.
 * Leak volume is tiny (FThread module vectors are few and small) -> no OOM. Scoped to Fred (HEROS_PROC_NAME~"mmi").
 *
 * Knob: FKEEPVEC=1. Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o fkeepvec.so emulator/fkeepvec.c */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

extern void __libc_free(void *);

/* ~FThread's `call _ZdlPv@plt` is at libbackend+0x26ebe (5 bytes) -> return address = libbackend+0x26ec3. */
#define FTHREAD_DTOR_DELVEC_RA 0x26ec3u

static int fred = -1;
static int in_fred(void){
    if(fred < 0){ const char *n = getenv("HEROS_PROC_NAME"); fred = (n && strstr(n, "mmi")) ? 1 : 0; }
    return fred;
}

/* libbackend.so text base (r-xp, file offset 0) from /proc/self/maps; cached. 0 until resolved. */
static unsigned long bb_base = 0;
static int bb_tried = 0;
static unsigned long backend_base(void){
    if(bb_base || bb_tried) return bb_base;
    bb_tried = 1;
    int fd = open("/proc/self/maps", O_RDONLY);
    if(fd < 0) return 0;
    char buf[8192]; char line[512]; int li = 0; ssize_t n;
    while((n = read(fd, buf, sizeof buf)) > 0){
        for(ssize_t i = 0; i < n; i++){
            char c = buf[i];
            if(c == '\n' || li >= (int)sizeof(line)-1){
                line[li] = 0;
                if(li && strstr(line, "libbackend.so") && strstr(line, "r-xp") && strstr(line, " 00000000 ")){
                    unsigned long s = 0; const char *p = line; int ok = 0;
                    for(; *p && *p != '-'; p++){
                        int d; char ch = *p;
                        if(ch>='0'&&ch<='9') d = ch-'0';
                        else if(ch>='a'&&ch<='f') d = ch-'a'+10;
                        else { ok = 0; break; }
                        s = (s<<4)|d; ok = 1;
                    }
                    if(ok){ bb_base = s; close(fd); return bb_base; }
                }
                li = 0;
            } else line[li++] = c;
        }
    }
    close(fd);
    return bb_base;
}

/* async-signal-safe one-liner "[fkeepvec] leaked <ptr>\n" to fd 2 (rare: only the ~FThread vector delete) */
static void logleak(void *p){
    char b[40]; int i = 0; const char *t = "[fkeepvec] leaked ";
    while(t[i]){ b[i] = t[i]; i++; }
    unsigned long v = (unsigned long)p;
    for(int k = 7; k >= 0; k--){ int d = (v >> (k*4)) & 0xf; b[i++] = d < 10 ? '0'+d : 'a'+d-10; }
    b[i++] = '\n';
    (void)write(2, b, i);
}

static int keep(void *ra){
    if(!in_fred()) return 0;
    unsigned long base = backend_base();
    if(!base) return 0;
    return ((unsigned long)ra) == base + FTHREAD_DTOR_DELVEC_RA;
}

void _ZdlPv(void *p){                              /* operator delete(void*) — the ~FThread vector delete site */
    if(p && keep(__builtin_return_address(0))){ logleak(p); return; }   /* withhold from reuse (leak) */
    __libc_free(p);
}
void _ZdlPvj(void *p, unsigned s){ (void)s;         /* sized delete (belt-and-suspenders; site uses unsized) */
    if(p && keep(__builtin_return_address(0))){ logleak(p); return; }
    __libc_free(p);
}
