/* throwcatch.c — i386 LD_PRELOAD: log every C++ throw (type + string + guest backtrace)
 * as it happens, then chain to the real __cxa_throw. The winmgr WmUsbThread crash is
 * an EvtExceptionShell RETRY-on-exception that desyncs the FModule eval-context; this
 * pins the FIRST/triggering throw's exact site + payload. For a `const char*` (typeid
 * "PKc") throw, *thrown is the char* message. Build:
 *   i686-linux-gnu-gcc -shared -fPIC -O2 -o throwcatch.so emulator/throwcatch.c -ldl
 * Preload FIRST. Diagnostic only. */
#define _GNU_SOURCE
#include <execinfo.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <stdint.h>

/* std::type_info layout: vptr, char* __name  (name at offset 4 on i386) */
static const char* ti_name(void *ti){ if(!ti) return "?"; return *(const char**)((char*)ti + 4); }

static void (*real_throw)(void*,void*,void(*)(void*)) = NULL;

/* plausible userspace pointer we can peek at (guarded best-effort) */
static int plausible(unsigned p){ return p>=0x08000000u && p<0xf8000000u; }
static int printable_str(const char *s){ if(!s) return 0; for(int i=0;i<4;i++){ unsigned char c=s[i]; if(c<0x20||c>0x7e) return 0;} return 1; }

void __cxa_throw(void *thrown, void *type, void (*dtor)(void*)){
    const char *tn = ti_name(type);
    char msg[256]; const char *payload="";
    if(tn && !strcmp(tn,"PKc") && thrown) payload = *(const char**)thrown;   /* const char* */
    int n = snprintf(msg,sizeof msg,"\n[throwcatch] THROW type=%s payload=\"%.120s\" tid=%d — site:\n",
                     tn?tn:"?", payload?payload:"", (int)gettid());
    (void)!write(2,msg,n);
    /* dump the thrown object's leading dwords + any embedded string (finds the Xml::Exception msg) */
    if(thrown){
        unsigned *o=(unsigned*)thrown;
        for(int i=0;i<10;i++){
            unsigned v=o[i];
            char line[160]; int ln;
            if(plausible(v) && printable_str((const char*)(uintptr_t)v))
                ln=snprintf(line,sizeof line,"  [obj+%d]=%08x -> \"%.80s\"\n",i*4,v,(const char*)(uintptr_t)v);
            else if(plausible(v) && printable_str((const char*)(uintptr_t)v+0))  /* astring inline? */
                ln=snprintf(line,sizeof line,"  [obj+%d]=%08x\n",i*4,v);
            else ln=snprintf(line,sizeof line,"  [obj+%d]=%08x\n",i*4,v);
            (void)!write(2,line,ln);
        }
    }
    void *bt[32]; int f = backtrace(bt,32);
    backtrace_symbols_fd(bt,f,2);
    if(!real_throw) real_throw = dlsym(RTLD_NEXT,"__cxa_throw");
    real_throw(thrown,type,dtor);
    __builtin_unreachable();
}
