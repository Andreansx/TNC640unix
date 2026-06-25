/* logspy.c — LD_PRELOAD diagnostic: make HEIDENHAIN HeLogging visible on stderr.
 *
 * HrMmi (and the rest of the control) emit rich internal diagnostics via
 *   LOGSEND(ostream) ... -> HeLogging::SendData2Logger(HELOGGING const&)
 * In the standalone 2-proc FEX setup no logger process is connected, so these
 * never reach stdout. This interposes SendData2Logger, safely locates the
 * formatted message string inside the HELOGGING object, and prints it to stderr.
 *
 * DIAGNOSTIC ONLY (env LOGSPY=1). It does NOT chain to the real SendData2Logger
 * (the real one would try to reach the absent logger). With it we can see exactly
 * how far HrModule::OnHrMmiCfgGlobal gets and whether
 * HrModule::MoveActiveStateTowardsTarget advances ("HrModule state: Adjusting...").
 *
 * ** KNOWN LIMITATION (verified 2026-06-25): the non-chaining no-op BREAKS HrMmi early **
 * — some early-startup code depends on SendData2Logger's side effects, so loading logspy
 * (LOGSPY=1) makes HrMmi SIGSEGV before config arrives. Also, most HrModule LOGSEND
 * messages are severity-FILTERED before SendData2Logger, so the trace is incomplete.
 * To make this useful, CHAIN to the real SendData2Logger (resolve it past this preload).
 * Left in the tree as a starting point; do NOT enable in a run that must reach config.
 *
 * Memory safety: the HELOGGING layout is not fully known, so we scan its first
 * words for pointers to printable text. Every candidate pointer is validated with
 * a write()-to-/dev/null EFAULT probe BEFORE dereference (a wild pointer must not
 * crash HrMmi — an earlier version did).
 *
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o logspy.so logspy.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

static int enabled = -1;
static int devnull = -1;
static void init_once(void){
    if(enabled<0){ const char*e=getenv("LOGSPY"); enabled = e&&e[0]=='1'; }
    if(devnull<0){ devnull = open("/dev/null", O_WRONLY); }
}
static int on(void){ init_once(); return enabled; }

/* readable(p,len): true iff [p,p+len) is readable, WITHOUT faulting.
 * write() to /dev/null returns -1/EFAULT for an unreadable buffer. */
static int readable(const void *p, size_t len){
    if((uintptr_t)p < 0x1000) return 0;
    if(devnull<0) return 0;
    ssize_t r = write(devnull, p, len);
    return r == (ssize_t)len;
}

/* Is p a readable, fully-printable C string of length in [minlen,511]? */
static const char* good_str(const void *p, int minlen){
    if(!readable(p, 1)) return 0;
    const unsigned char *s = (const unsigned char*)p;
    int n=0;
    for(; n<512; n++){
        if(!readable(s+n, 1)) return 0;          /* page boundary / unmapped */
        unsigned char c = s[n];
        if(c==0) break;
        if(!(c=='\n'||c=='\t'||c=='\r' || (c>=0x20 && c<0x7f))) return 0;
    }
    if(n<minlen || n>=512) return 0;
    return (const char*)s;
}

/* The HELOGGING object is small; scan its first words for a char-pointer or
 * std::string pointer (incl. SSO: a self-pointer into the object) to a printable
 * message; print the LONGEST such string. LOGSPY_RAW=1 also dumps the raw words. */
static int call_n=0;
static void dump_helogging(const void *hel){
    if(!hel || !readable(hel, 4)) return;
    call_n++;
    static int raw=-1; if(raw<0){ const char*e=getenv("LOGSPY_RAW"); raw=e&&e[0]=='1'; }
    const char *best=0; int bestlen=0;
    for(int i=0;i<32;i++){
        if(!readable((const char*)hel + i*4, 4)) break;
        uintptr_t w; memcpy(&w, (const char*)hel + i*4, 4);
        const char *s = good_str((const void*)w, 4);
        /* also try the word value itself as inline text (SSO/embedded chars) */
        if(s){ int l=(int)strlen(s); if(l>bestlen){ best=s; bestlen=l; } }
    }
    if(raw){
        fprintf(stderr,"[logspy#%d] raw:",call_n);
        for(int i=0;i<20 && readable((const char*)hel+i*4,4);i++){ uintptr_t w; memcpy(&w,(const char*)hel+i*4,4); fprintf(stderr," %08lx",(unsigned long)w); }
        fprintf(stderr,"\n");
    }
    if(best) fprintf(stderr, "[logspy#%d] %s\n", call_n, best);
    else     fprintf(stderr, "[logspy#%d] (no string found)\n", call_n);
    fflush(stderr);
}

__attribute__((constructor)) static void logspy_ctor(void){
    init_once();
    if(enabled) fprintf(stderr, "[logspy] loaded (interposing HeLogging::SendData2Logger)\n");
}

/* void HeLogging::SendData2Logger(HeLogging *this, const HELOGGING *data) — i386 member ABI: stack args. */
void _ZN9HeLogging15SendData2LoggerERK9HELOGGING(void *thisp, const void *data){
    (void)thisp;
    if(on()) dump_helogging(data);
    /* no-op: do not chain (no logger peer in the standalone setup) */
}
