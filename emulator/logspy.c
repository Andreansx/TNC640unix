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
 * ** KNOWN LIMITATION (verified 2026-06-25): loading logspy BREAKS HrMmi early — even CHAINED. **
 * The no-op version SIGSEGV'd HrMmi before config. This version CHAINS to the real SendData2Logger
 * (resolved via dl_iterate_phdr + a manual DT_HASH scan — NOT dlsym, which is dlsym@GLIBC_2.34 and
 * silently breaks FEX preloads, same lesson as cfgfix), yet loading logspy STILL SIGSEGVs HrMmi early
 * (the crash is a deeper interposition side effect, not the missing chain). ⇒ logspy is a DEAD
 * diagnostic for HrMmi under FEX; do not enable it in a run that must reach config. (Moot anyway:
 * HrMmi is the HANDWHEEL MMI, not the main screen — the real main MMI is Guppy.elf.)
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

/* CHAIN to the real SendData2Logger so the control's startup side effects still
 * happen (the non-chaining no-op SIGSEGV'd HrMmi before config). dlsym(RTLD_NEXT)
 * is dlsym@GLIBC_2.34 which silently breaks the FEX preload (same lesson as cfgfix),
 * so resolve the real symbol via dl_iterate_phdr (GLIBC_2.2.4, always present) +
 * a manual DT_HASH symbol scan, skipping our own definition. */
#include <link.h>
#include <elf.h>
typedef void (*sd2l_t)(void*, const void*);
static sd2l_t real_sd2l = 0;
static const char *WANT = "_ZN9HeLogging15SendData2LoggerERK9HELOGGING";

struct findctx { void *self; sd2l_t found; };
static int phdr_cb(struct dl_phdr_info *info, size_t sz, void *arg){
    (void)sz; struct findctx *c = (struct findctx*)arg;
    const ElfW(Dyn) *dyn = 0;
    for(int i=0;i<info->dlpi_phnum;i++)
        if(info->dlpi_phdr[i].p_type == PT_DYNAMIC)
            dyn = (const ElfW(Dyn)*)(info->dlpi_addr + info->dlpi_phdr[i].p_vaddr);
    if(!dyn) return 0;
    const ElfW(Sym) *symtab=0; const char *strtab=0; const ElfW(Word) *hash=0;
    for(const ElfW(Dyn)*d=dyn; d->d_tag!=DT_NULL; d++){
        if(d->d_tag==DT_SYMTAB) symtab=(const ElfW(Sym)*)d->d_un.d_ptr;
        else if(d->d_tag==DT_STRTAB) strtab=(const char*)d->d_un.d_ptr;
        else if(d->d_tag==DT_HASH) hash=(const ElfW(Word)*)d->d_un.d_ptr;
    }
    if(!symtab||!strtab||!hash) return 0;
    ElfW(Word) nchain = hash[1];                 /* nchain == number of dynsyms */
    for(ElfW(Word) k=0;k<nchain;k++){
        const ElfW(Sym)*s=&symtab[k];
        if(!s->st_value || ELF32_ST_TYPE(s->st_info)!=STT_FUNC) continue;
        if(strcmp(strtab + s->st_name, WANT)) continue;
        void *addr = (void*)(info->dlpi_addr + s->st_value);
        if(addr == c->self) continue;            /* skip our own interposed copy */
        c->found = (sd2l_t)addr; return 1;
    }
    return 0;
}

void _ZN9HeLogging15SendData2LoggerERK9HELOGGING(void *thisp, const void *data);

__attribute__((constructor)) static void logspy_ctor(void){
    init_once();
    struct findctx c = { (void*)&_ZN9HeLogging15SendData2LoggerERK9HELOGGING, 0 };
    dl_iterate_phdr(phdr_cb, &c);
    real_sd2l = c.found;
    if(enabled) fprintf(stderr, "[logspy] loaded (interposing+chaining HeLogging::SendData2Logger, real=%p)\n", (void*)real_sd2l);
}

/* void HeLogging::SendData2Logger(HeLogging *this, const HELOGGING *data) — i386 member ABI: stack args. */
void _ZN9HeLogging15SendData2LoggerERK9HELOGGING(void *thisp, const void *data){
    if(on()) dump_helogging(data);
    if(real_sd2l) real_sd2l(thisp, data);   /* chain: keep the real side effects */
}
