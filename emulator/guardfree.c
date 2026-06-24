/* guardfree.c — i386 LD_PRELOAD: a GUARDED free that frees VALID heap pointers normally but SKIPS the
 * invalid one ConfigServer over-frees while handling HrMmi's 0x170501 CfgGetData under FEX.
 *
 * Why not noopfree (leak everything): its leaks leave stale memory that ConfigServer's config-init
 * GMessage deserializer mis-reads -> a "CfgUnitOfMeasure" type-exception that derails the config load.
 * Why not a plain alignment check: i386 glibc malloc is only 8-aligned, and the bad pointer is an
 * 8-aligned value mis-derived from the message bytes (out of the heap range) — alignment passes it.
 *
 * guardfree validates each pointer against /proc/self/maps WRITABLE regions: a real heap/mmap chunk is
 * inside one; a value mis-read from a message is not -> skip (leak one chunk) instead of letting glibc
 * abort. Valid frees go through untouched, so there is no stale-memory CfgUnitOfMeasure throw.
 * A thread-local re-entry guard frees normally while load_maps() itself allocates/frees (no recursion).
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o guardfree.so emulator/guardfree.c -ldl */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

static void (*real_free)(void *) = 0;
static __thread int reentry = 0;

#define MAXREG 1024
static uintptr_t reg_lo[MAXREG], reg_hi[MAXREG];
static volatile int nreg = 0;

static void load_maps(void){
    FILE *f = fopen("/proc/self/maps", "r");
    if(!f) return;
    char line[512]; int n = 0;
    while(n < MAXREG && fgets(line, sizeof line, f)){
        unsigned long lo, hi; char perms[8] = {0};
        if(sscanf(line, "%lx-%lx %7s", &lo, &hi, perms) == 3 && perms[1] == 'w'){
            reg_lo[n] = (uintptr_t)lo; reg_hi[n] = (uintptr_t)hi; n++;
        }
    }
    fclose(f);
    nreg = n;   /* publish last (readers tolerate a slightly stale count) */
}

static int in_writable(uintptr_t a){
    int n = nreg;
    for(int i = 0; i < n; i++) if(a >= reg_lo[i] && a < reg_hi[i]) return 1;
    return 0;
}

__attribute__((constructor)) static void gf_init(void){ load_maps(); }

void free(void *p){
    if(!p) return;
    if(!real_free) real_free = dlsym(RTLD_NEXT, "free");
    if(reentry){ real_free(p); return; }       /* inside load_maps: free normally, no recursion */
    uintptr_t a = (uintptr_t)p;
    if(a & 0x7) return;                          /* misaligned -> not a glibc chunk */
    if(!in_writable(a)){
        reentry = 1; load_maps(); reentry = 0;   /* refresh (heap/mmap grew) then re-check */
        if(!in_writable(a)) return;              /* still not a mapped writable chunk -> skip the bad free */
    }
    /* The bad free is an IN-HEAP pointer (passes the maps check) that is NOT a real chunk start: glibc
     * "free(): invalid pointer" = its size word at p-4 is garbage (so p > -size). p is mapped+writable,
     * so reading the chunk header is safe. Validate it: a real i386 glibc chunk has size (low 3 flag bits
     * masked) in [MINSIZE, sane-max] and its successor header lands in a mapped writable region (unless
     * the chunk is mmap'd, IS_MMAPPED bit 1). A value mis-read from the message fails this -> skip. */
    /* Validate the chunk SIZE word only. The bad over-free has a zeroed/garbage header (observed raw=0,
     * sz=0), so a sane-size check catches it. A successor-mapped check was too strict — it wrongly skipped
     * valid chunks whose successor lands at a region boundary / a region the cached maps missed, leaking
     * them -> stale memory -> the CfgUnitOfMeasure throw. Size-only frees valid chunks (sz 48, 4112, ...). */
    uint32_t raw = *(volatile uint32_t *)(a - 4);
    uint32_t sz  = raw & ~(uint32_t)0x7;
    if(sz < 16u || sz > 0x4000000u){ if(getenv("GUARDFREE_LOG")) fprintf(stderr,"[guardfree] SKIP %p raw=%08x sz=%u\n",p,raw,sz); return; }
    real_free(p);
}

/* C++ operator delete family -> route through the guard too */
void _ZdlPv(void *p){ free(p); }
void _ZdaPv(void *p){ free(p); }
void _ZdlPvj(void *p, unsigned int s){ (void)s; free(p); }
void _ZdaPvj(void *p, unsigned int s){ (void)s; free(p); }
