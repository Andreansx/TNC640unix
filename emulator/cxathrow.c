/* cxathrow.c — LD_PRELOAD __cxa_throw interceptor (i386, for FEX-run control procs).
 *
 * Purpose: pin the EXACT throw site of an uncaught C++ exception (e.g. promview's
 * `terminate: PciHardware::Exception`). Interposing __cxa_throw is un-fakeable — the
 * DIRECT caller of __cxa_throw IS the throwing statement, so __builtin_return_address(0)
 * gives the throw site with zero ambiguity. We resolve it to lib+offset by reading
 * /proc/self/maps OURSELVES (dladdr/dlsym are unreliable in FEX preloads — see the
 * cfg461probe note in CLAUDE.md), then re-raise via the real __cxa_throw so behaviour
 * is unchanged. The one dlsym (for the real __cxa_throw) has an abort() fallback; since
 * the exceptions we chase are uncaught (they reach terminate->abort anyway), the
 * fallback is behaviourally identical for the crash we are diagnosing.
 *
 * type_info layout (Itanium C++ ABI, 32-bit): +0 vptr, +4 const char* __name.
 * Enable by prepending /lib/cxathrow.so to the guest LD_PRELOAD.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <dlfcn.h>

static void resolve_addr(const char *tag, uintptr_t addr){
    FILE *f = fopen("/proc/self/maps","r");
    if(!f){ fprintf(stderr,"[cxathrow]   %s=%p (no maps)\n",tag,(void*)addr); return; }
    char line[600];
    while(fgets(line,sizeof line,f)){
        uintptr_t lo=0,hi=0; char path[512]; path[0]=0;
        /* addr-hi perms off dev inode path */
        if(sscanf(line,"%lx-%lx %*s %*s %*s %*s %511[^\n]",&lo,&hi,path)>=2){
            if(addr>=lo && addr<hi){
                const char *p=path; while(*p==' ') p++;
                fprintf(stderr,"[cxathrow]   %s=%p  %s +0x%lx\n",
                        tag,(void*)addr, *p?p:"[anon]", (unsigned long)(addr-lo));
                fclose(f); return;
            }
        }
    }
    fprintf(stderr,"[cxathrow]   %s=%p (unmapped)\n",tag,(void*)addr);
    fclose(f);
}

typedef void (*throw_fn)(void*,void*,void(*)(void*));

void __cxa_throw(void *obj, void *tinfo, void (*dest)(void*)){
    const char *name = "?";
    if(tinfo){
        const char *n = *(const char* const*)((const char*)tinfo + 4);
        if(n) name = n;
    }
    void *site = __builtin_return_address(0);
    /* PciHardware::Exception (and most control exceptions) wrap a single int reason
     * code as their first member — print it; it discriminates which throw statement fired. */
    int code = obj ? *(const int*)obj : -1;
    fprintf(stderr,"[cxathrow] THROW type='%s' code=%d obj=%p\n", name, code, obj);
    resolve_addr("site", (uintptr_t)site);
    fflush(stderr);

    throw_fn real = (throw_fn)dlsym(RTLD_NEXT,"__cxa_throw");
    if(real){ real(obj,tinfo,dest); }
    fprintf(stderr,"[cxathrow] (real __cxa_throw unavailable) aborting\n");
    abort();
}
