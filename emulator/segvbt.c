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

#define NSIG_TRACK 6
static const int tracked[NSIG_TRACK] = { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP };
static struct sigaction guest_sa[NSIG_TRACK];   /* the handler the guest wanted */
static int have_guest[NSIG_TRACK];

static int idx_of(int sig){ for(int i=0;i<NSIG_TRACK;i++) if(tracked[i]==sig) return i; return -1; }

static void dump(int sig, void *addr){
    char hdr[160];
    int n = snprintf(hdr,sizeof hdr,
        "\n[segvbt] ===== fatal signal %d fault_addr=%p tid=%d — guest backtrace =====\n",
        sig, addr, (int)gettid());
    (void)!write(2,hdr,n);
    void *bt[128];
    int f = backtrace(bt,128);
    backtrace_symbols_fd(bt,f,2);
    (void)!write(2,"[segvbt] ===== end backtrace =====\n",34);
}

static void wrapper(int sig, siginfo_t *si, void *uc){
    dump(sig, si?si->si_addr:0);
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
