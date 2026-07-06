/* segvbt.c — i386 LD_PRELOAD: dump a GUEST backtrace when a fatal signal
 * (SIGSEGV/SIGBUS/SIGILL/SIGFPE/SIGABRT) is delivered, BEFORE the guest's own
 * handler (e.g. libheros_sigfaterr) runs. winmgr installs its own SIGSEGV handler,
 * so we interpose sigaction()/signal(): when the guest registers a handler for a
 * fatal signal we record it and install OUR wrapper instead. On the signal the
 * wrapper prints the fault address + a guest backtrace (winmgr.elf is NOT stripped
 * -> backtrace_symbols_fd yields function names), then chains to the guest handler
 * so the normal termination path is preserved.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o segvbt.so emulator/segvbt.c -ldl
 * Preload FIRST. Diagnostic only. */
#define _GNU_SOURCE
#include <execinfo.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
#include <unistd.h>
#include <ucontext.h>
#include <stdint.h>
#include <fcntl.h>

#define NSIG_TRACK 6
static const int tracked[NSIG_TRACK] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
static struct sigaction guest_sa[NSIG_TRACK];   /* the handler the guest wanted */
static int have_guest[NSIG_TRACK];

static int idx_of(int sig){ for(int i=0;i<NSIG_TRACK;i++) if(tracked[i]==sig) return i; return -1; }

/* dump /proc/self/maps ONCE (so EIP/fault_addr can be resolved to a module+offset) */
static void dump_maps_once(void){
    static int done=0; if(done) return; done=1;
    int fd=open("/proc/self/maps",O_RDONLY); if(fd<0) return;
    (void)!write(2,"[segvbt] ---- /proc/self/maps (winmgr.elf + libheros lines) ----\n",64);
    char b[4096]; ssize_t n;
    /* stream the whole file to stderr; caller greps for winmgr/heros */
    while((n=read(fd,b,sizeof b))>0) (void)!write(2,b,n);
    close(fd);
    (void)!write(2,"[segvbt] ---- end maps ----\n",28);
}

static void dump(int sig, void *addr, void *ucv){
    char hdr[512];
    ucontext_t *uc=(ucontext_t*)ucv;
    unsigned int eip=0,ebp=0,esp=0,eax=0,ebx=0,ecx=0,edx=0,esi=0,edi=0;
#if defined(__i386__)
    if(uc){ greg_t *g=uc->uc_mcontext.gregs;
        eip=g[REG_EIP]; ebp=g[REG_EBP]; esp=g[REG_ESP];
        eax=g[REG_EAX]; ebx=g[REG_EBX]; ecx=g[REG_ECX]; edx=g[REG_EDX];
        esi=g[REG_ESI]; edi=g[REG_EDI]; }
#endif
    int n = snprintf(hdr,sizeof hdr,
        "\n[segvbt] ===== fatal signal %d fault_addr=%p tid=%d =====\n"
        "[segvbt] EIP=%08x ESP=%08x EBP=%08x\n"
        "[segvbt] EAX=%08x EBX=%08x ECX=%08x EDX=%08x ESI=%08x EDI=%08x\n",
        sig, addr, (int)gettid(), eip, esp, ebp, eax, ebx, ecx, edx, esi, edi);
    (void)!write(2,hdr,n);
    dump_maps_once();
    /* manual EBP-chain walk (i386): [ebp]=saved ebp, [ebp+4]=return addr.
     * More robust than backtrace() for FEX-translated sub-threads. */
    (void)!write(2,"[segvbt] EBP-chain return addrs:\n",32);
    unsigned int fp=ebp; unsigned int prev=0;
    for(int i=0;i<32 && fp && fp>0x1000 && fp!=prev;i++){
        unsigned int *frame=(unsigned int*)(uintptr_t)fp;
        unsigned int ret, next;
        /* guard the reads: if they fault we'd recurse — but we're already in the
         * handler, so keep it simple and rely on the frame pointers being sane */
        ret = frame[1]; next = frame[0];
        char lb[48]; int ln=snprintf(lb,sizeof lb,"[segvbt]   #%d ret=%08x fp=%08x\n",i,ret,fp);
        (void)!write(2,lb,ln);
        prev=fp; fp=next;
    }
    void *bt[128];
    int f = backtrace(bt,128);
    backtrace_symbols_fd(bt,f,2);
    (void)!write(2,"[segvbt] ===== end =====\n",25);
}

static void wrapper(int sig, siginfo_t *si, void *uc){
    dump(sig, si?si->si_addr:0, uc);
    int i = idx_of(sig);
    if(i>=0 && have_guest[i]){
        struct sigaction *g = &guest_sa[i];
        if(g->sa_flags & SA_SIGINFO){
            if(g->sa_sigaction) { g->sa_sigaction(sig,si,uc); return; }
        } else {
            if(g->sa_handler==SIG_IGN) return;
            if(g->sa_handler && g->sa_handler!=SIG_DFL){ g->sa_handler(sig); return; }
        }
    }
    /* default: re-raise with default disposition so the process dies as usual */
    signal(sig, SIG_DFL);
    raise(sig);
}

static int (*real_sigaction)(int,const struct sigaction*,struct sigaction*) = NULL;

int sigaction(int sig, const struct sigaction *act, struct sigaction *old){
    if(!real_sigaction) real_sigaction = dlsym(RTLD_NEXT,"sigaction");
    int i = idx_of(sig);
    if(i>=0 && act){
        guest_sa[i] = *act; have_guest[i]=1;               /* remember what the guest wanted */
        struct sigaction mine = *act;
        mine.sa_flags |= SA_SIGINFO;
        mine.sa_sigaction = wrapper;
        int rc = real_sigaction(sig,&mine,old);
        if(old && have_guest[i]) { /* report the previously-recorded guest handler, not our wrapper */ }
        return rc;
    }
    return real_sigaction(sig,act,old);
}

/* signal() -> route through sigaction so we capture it too */
typedef void (*sighandler_t)(int);
sighandler_t signal(int sig, sighandler_t h){
    int i = idx_of(sig);
    if(i>=0){
        struct sigaction sa; memset(&sa,0,sizeof sa);
        sa.sa_handler = h; sigemptyset(&sa.sa_mask);
        struct sigaction old;
        if(sigaction(sig,&sa,&old)!=0) return SIG_ERR;
        return old.sa_handler;
    }
    static sighandler_t (*rs)(int,sighandler_t)=NULL;
    if(!rs) rs = dlsym(RTLD_NEXT,"signal");
    return rs(sig,h);
}
