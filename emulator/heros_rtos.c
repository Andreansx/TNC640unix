/* heros_rtos.c — a faithful-enough HeROS RTOS runtime, in userspace, for the i386
 * TNC640 control. LD_PRELOAD interposes libc syscall() and emulates HeROS syscall
 * 222 (heroscall, cmd=0x1234_00NN) with REAL blocking primitives so the genuine
 * multi-threaded HeROS framework (FThread/FEvent/FProcess/FSemaphore) runs.
 *
 * State lives in a /dev/shm shared segment so it survives fork()+execve() and is
 * shared across the whole process constellation (AppStart + its children). Blocking
 * is futex-based on words inside that segment (process-shared). Everything inside
 * the segment is referenced by INDEX, never by pointer (mappings differ per proc).
 *
 * Primitives (ABI recovered from heros.ko / heros_ko.decomp.c + live param dumps):
 *   T_ident 01  name inline p[0..1] (0=self) -> task id in eax
 *   T_name  09  set current task name (p[0]=name)
 *   Ev_send 10  p[0]=target task id, p[1]=bits           -> 0/err
 *   Ev_recv 11  p[0]=wanted bits, p[1]=cond(1=ALL,2=ANY), p[2]=timeout -> event word in eax
 *   Sm_create 15  p[0]=name? p[2]=count ... -> sem id in eax
 *   Sm_ident  16  name -> sem id
 *   Sm_request18  p[0]=sem id, p[1]=flags, p[2]=timeout   -> 0/err  (P/wait)
 *   Sm_release19  p[0]=sem id                             -> 0/err  (V/signal)
 *   Q_create  0a  -> queue id ;  Q_send 0d ;  Q_read 0e
 *   M_ident 22 / M_attach 23  named shared region (-> /dev/shm)
 *   Sys_getenv 27  p[0]=name p[2]=outbuf p[4]=size
 * Unknowns are logged (HEROSCALL_VERBOSE=1) and return success(0).
 *
 * Build (i386): gcc -m32 -shared -fPIC -O2 -o heros_rtos.so heros_rtos.c -lpthread
 */
#define _GNU_SOURCE
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/futex.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <signal.h>
#include <ucontext.h>
#include <sched.h>
#include <pthread.h>

long syscall(long n, ...);
/* raw 5-arg syscall (i386 int 0x80). All syscalls we issue fit in <=5 args
 * (futex WAIT/WAKE need <=4); mmap/ftruncate/open use libc (no recursion into
 * our interposed syscall(), since glibc uses inline syscalls internally). */
static long raw5(long n,long a,long b,long c,long d,long e){ long r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e):"memory"); return r; }

static int vrb=0, btrace_on=0;
#define LOG(...) do{ if(vrb) fprintf(stderr,"[rtos] " __VA_ARGS__); }while(0)
static const char* hcname(int lo);
/* unbuffered debug via raw write(2) — survives early-constructor / pre-crash */
static void dbg(const char*s){ int n=0; while(s[n])n++; raw5(SYS_write,2,(long)s,n,0,0); }

/* ---- compact HANDSHAKE TRACE (HEROSCALL_HSTRACE=1) ----------------------------
 * Low-volume protocol trace of the event/queue-notify + thread-rendezvous calls,
 * to observe the parent/child startup handshake (the 0x106<->0x108 ping-pong on
 * the logo). VERBOSE is ~1.5M lines / unusable for this; HSTRACE logs ONE compact
 * line per Ev_send / blocking-or-nonzero Ev_receive / Q_create / Q_send-notify /
 * Q_read / T_create / T_start. Optional task filter HEROSCALL_HSTRACE_TASKS=106,108
 * (hex ids) limits output to lines that touch one of those tasks. */
static int hstrace=0;
static uint32_t hst_tasks[16]; static int hst_ntasks=0;
static int hst_hit(uint32_t a,uint32_t b){
    if(hst_ntasks==0) return 1;
    for(int i=0;i<hst_ntasks;i++) if(hst_tasks[i]==a||hst_tasks[i]==b) return 1;
    return 0;
}
#define HST(a,b,...) do{ if(hstrace && hst_hit((a),(b))) fprintf(stderr,"[hst] " __VA_ARGS__); }while(0)
/* printable-ASCII view of a message body (for reading embedded subsystem names in FmProcessState etc.) */
static const char* msascii(const void*p,uint32_t n){
    static char b[160]; uint32_t j=0; if(!p){b[0]=0;return b;} if(n>140)n=140;
    for(uint32_t k=0;k<n&&j<sizeof(b)-1;k++){ unsigned char c=((const unsigned char*)p)[k]; b[j++]=(c>=32&&c<127)?c:'.'; }
    b[j]=0; return b;
}

/* ---------------- shared control segment ---------------- */
#define MAXTASK 512
#define MAXSEM  512
#define MAXQ    2048       /* ConfigServer registers a ~1000-entry HwsM<task>N<ctr> mailslot pool
                            * at startup; 96 overflowed → "table full" → a retry spin (1.4GB log). */
#define MAXREG  64
#define QSLOTS  12         /* messages buffered per queue                        */
#define QMSGCAP 16384      /* max bytes per message (kernel caps Q_send @ 0x8000) */
#define NAMELEN 32

struct task { int used; uint32_t id; int32_t tgid, tid; char name[NAMELEN];
              volatile uint32_t events;              /* futex word (events) */
              volatile uint32_t started;             /* futex word (t_create<->t_start rendezvous) */
              uint32_t ctx_dst, arg_dst, msgsize;    /* delivery slots (process-local ptrs) */
              volatile uint32_t as_pending;          /* async-signals raised but not yet read (TCB+0x1f0) */
              volatile uint32_t as_mask;             /* enabled async-signal mask          (TCB+0x1f4) */
              volatile uint32_t last_ev_want;        /* most recent Ev_receive want-mask (for queue-notify bit matching) */
              volatile uint32_t nowait_miss;         /* consecutive Ev_receive(EV_NOWAIT) misses (for EV_NOWAIT_YIELD spin throttle) */
              volatile uint32_t autofire_n;          /* count of SYSEVENT_AUTOFIRE fires for this task (FIRE_LIMIT cap: fire enough to clear startup render-waits, then block normally so the serve thread runs) */
              volatile uint32_t last_req_family; };  /* type-id>>16 of the last request this task sent to a server queue (CfgServerQueue/Q_SkMgr*) — used to route its reply on the SHARED ".Rts" per-process reply queue (RTS_FAMILY_ROUTE) */
struct sem  { int used; uint32_t id; char name[NAMELEN]; volatile int32_t count; };
/* one variable-length message + the 12-byte sender header the kernel returns in Q_read's p[4]
 * (from Q_send's message-node fields: source queue id, sender task, mode|size) */
struct qmsg { uint32_t len; uint32_t hdr[3]; uint8_t data[QMSGCAP]; };
struct queue{ int used; uint32_t id; char name[NAMELEN];
              uint32_t depth, flags;                  /* from Q_create (advisory)         */
              uint32_t owner, notify_bits;            /* Q_send Ev_sends notify_bits->owner (kernel +0xb8/+0xe8) */
              volatile uint32_t head, tail;           /* tail-head = count; futex on tail */
              struct qmsg msg[QSLOTS]; };
struct region{int used; uint32_t id; char name[20]; uint32_t size; };

#define EVTRING 32
#define EVTMSGCAP 16384   /* must hold the big config broadcast (the full ~4380B config payload) */
struct ctl {
    volatile int magic;                              /* 0=empty 1=initing 2=ready */
    volatile int32_t lock;                           /* futex spinlock */
    volatile uint32_t next_task, next_sem, next_q, next_reg;
    volatile uint32_t orphan_tc;                      /* pending main-context t_create rendezvous tokens
                                                        (child registered with parent=0xffffffff -> the
                                                        0x80000 wake had no valid target; a 0x80000-waiter
                                                        consumes one in ev_receive). See case 0x00. */
    struct task   tasks[MAXTASK];
    struct sem    sems[MAXSEM];
    struct queue  queues[MAXQ];
    struct region regs[MAXREG];
    /* QEvtServer late-subscriber catch-up (HEROS_EVT_RELAY): ConfigServer broadcasts config to
     * QEvtServer DURING RUN-UP, before HrMmi exists. Buffer those broadcasts here so they can be
     * flushed to a subscriber queue (HrMmi's QueueHrMmi) when it is created — emulating the event
     * server's fan-out to a late subscriber. */
    volatile uint32_t evt_ring_n;                    /* total broadcasts seen (ring index = n % EVTRING) */
    struct { uint32_t len; uint8_t data[EVTMSGCAP]; } evt_ring[EVTRING];
};
static struct ctl *C;

static int futex(volatile void *a,int op,int val,const struct timespec *t){
    return (int)raw5(SYS_futex,(long)a,op,val,(long)t,0);   /* uaddr2/val3 unused for WAIT/WAKE */
}
static void lock(void){
    while(__atomic_exchange_n(&C->lock,1,__ATOMIC_ACQUIRE))
        futex(&C->lock,FUTEX_WAIT,1,0);
}
static void unlock(void){
    __atomic_store_n(&C->lock,0,__ATOMIC_RELEASE);
    futex(&C->lock,FUTEX_WAKE,1,0);
}

static void ctl_init(void){
    const char *path="/dev/shm/heros_rtos_ctl";
    int fd=(int)raw5(SYS_openat,AT_FDCWD,(long)path,O_RDWR|O_CREAT,0600,0);
    if(fd<0){ fprintf(stderr,"[rtos] cannot open %s (%d)\n",path,fd); _exit(97); }
    raw5(SYS_ftruncate,fd,sizeof(struct ctl),0,0,0);
    void *m=mmap(0,sizeof(struct ctl),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    raw5(SYS_close,fd,0,0,0,0);
    if(m==MAP_FAILED){ fprintf(stderr,"[rtos] mmap ctl failed\n"); _exit(96); }
    C=(struct ctl*)m;
    /* first creator initialises; others wait for magic==2 */
    int exp=0;
    if(__atomic_compare_exchange_n(&C->magic,&exp,1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){
        C->lock=0; C->next_task=0x100; C->next_sem=0x200; C->next_q=0x300; C->next_reg=0x4000;
        __atomic_store_n(&C->magic,2,__ATOMIC_RELEASE);
        LOG("control segment created\n");
    } else {
        while(__atomic_load_n(&C->magic,__ATOMIC_ACQUIRE)!=2) ;
    }
}

/* ---------------- tasks ----------------
 * No `__thread` cache: TLS pulls a GLIBC_ABI_GNU_TLS verneed that the control's
 * glibc 2.31 lacks (the .so then won't load under qemu-i386 on ARM64). task_self()
 * finds-or-creates the task entry idempotently, so re-resolving per call is correct;
 * a non-TLS per-tid cache below keeps it cheap without the TLS ABI dependency. */
static int task_slot(uint32_t id){ for(int i=0;i<MAXTASK;i++) if(C->tasks[i].used&&C->tasks[i].id==id) return i; return -1; }

/* process-local (not shared) tid->id cache; benign races only (a miss just rescans) */
static struct { int32_t tid; uint32_t id; } tcache[128];
static uint32_t task_self(void){
    int32_t tid =(int32_t)raw5(SYS_gettid,0,0,0,0,0);
    unsigned h=((unsigned)tid)&127;
    if(tcache[h].id && tcache[h].tid==tid) return tcache[h].id;
    int32_t tgid=(int32_t)raw5(SYS_getpid,0,0,0,0,0);
    lock();
    for(int i=0;i<MAXTASK;i++) if(C->tasks[i].used&&C->tasks[i].tgid==tgid&&C->tasks[i].tid==tid){
        uint32_t id=C->tasks[i].id; unlock(); tcache[h].tid=tid; tcache[h].id=id; return id; }
    int s=-1; for(int i=0;i<MAXTASK;i++) if(!C->tasks[i].used){ s=i; break; }
    if(s<0){ unlock(); return 0x101; }
    C->tasks[s].used=1; C->tasks[s].id=C->next_task++; C->tasks[s].tgid=tgid; C->tasks[s].tid=tid;
    C->tasks[s].events=0; C->tasks[s].name[0]=0;
    C->tasks[s].as_pending=0; C->tasks[s].as_mask=0;
    uint32_t id=C->tasks[s].id; unlock();
    tcache[h].tid=tid; tcache[h].id=id;
    LOG("task_self -> new id 0x%x (tgid %d tid %d)\n",id,tgid,tid);
    return id;
}
static uint32_t task_by_name(const char*nm){
    lock();
    for(int i=0;i<MAXTASK;i++) if(C->tasks[i].used&&!strncmp(C->tasks[i].name,nm,NAMELEN-1)){
        uint32_t id=C->tasks[i].id; unlock(); return id; }
    unlock(); return 0;
}

/* ---------------- /dev/events wake bridge ----------------------------------------
 * The libbackend EVHandler GUI/IO threads (logo, HrMmi, …) do NOT block in Ev_receive
 * — they block in select() on /dev/events, the HeROS sysevent signaler fd. The real
 * kernel makes /dev/events READABLE whenever Ev_sendtcb delivers a matching sysevent to
 * that task, so the EVHandler's select() wakes and handlesysevents() then ev_receive()s
 * the bit and dispatches it (FWaitableQueue::Handle -> reads the queue). EVHandlerHandleIO
 * Event NEVER read()s the fd (verified @0x3dcb0) — it is purely a select() trigger.
 *
 * The emulator's ev_send only sets the event word + futex-wakes it, which wakes an
 * Ev_receive blocker but NOT a select() blocker. So a select()-blocked GUI thread never
 * wakes for a queue notify -> the logo handshake deadlocks (t106 forwards 5 msgs to the
 * logo queue 0x313 notifying t108 on 0x01000000, but t108 is in select() and never reads
 * them, so it never confirms and t106 waits forever).
 *
 * Fix: mirror the kernel's event->fd bridge. herosapi_shim creates a per-thread /dev/events
 * PIPE (HEROS_EVENTS_PIPE=1) and registers (rd,wr) here; ioctl(0x4502,&mask) registers the
 * enabled sysevent mask. On every event-word change for a task that owns a /dev/events pipe
 * IN THIS PROCESS, we reconcile its pipe readability to (pending events & (sysmask|OWNEVMASK))
 * != 0 — readable exactly when the kernel would signal a sysevent, drained when it clears.
 * OWNEVMASK (0xff000000) is always included: queue notifies (bits 24-31, EVHandlerCreateQueue)
 * are the critical wakes and must work even before the sysmask ioctl is seen. Same-process only
 * (the pipe fd is process-local); cross-process targets wake via the event-word futex (config
 * peers use Ev_receive, not select). */
#define MAXEVDEV 64
static struct evdev { volatile uint32_t task; int rd, wr; uint32_t sysmask; int signaled; } evdevs[MAXEVDEV];
static volatile int n_evdev=0;
static int events_bridge=0;                      /* HEROS_EVENTS_PIPE=1 -> /dev/events is a pipe */
static void evdev_reconcile_locked(struct evdev*e){      /* caller holds C->lock */
    int s=task_slot(e->task); if(s<0) return;
    uint32_t want = C->tasks[s].events & (e->sysmask | 0xff000000u);
    if(want){
        if(!e->signaled){ char c=1; raw5(SYS_write,e->wr,(long)&c,1,0,0); e->signaled=1; }
    } else if(e->signaled){
        char b[64]; while(raw5(SYS_read,e->rd,(long)b,sizeof b,0,0)>0) ; e->signaled=0;
    }
}
static void evdev_reconcile(uint32_t task){              /* find task's pipe (this process) + reconcile */
    if(!events_bridge||n_evdev==0) return;
    lock();
    for(int i=0;i<n_evdev;i++) if(evdevs[i].task==task){ evdev_reconcile_locked(&evdevs[i]); break; }
    unlock();
}
/* ---- CROSS-PROCESS /dev/events wake (watcher thread) -----------------------------------------
 * evdev_reconcile() pokes a task's /dev/events pipe IN THIS PROCESS only (the wr fd is process-local).
 * So an ev_send from ANOTHER process (e.g. Guppy's SkMgrLogin notify 0x02000000 -> skmgr's task 0x106)
 * sets the SHARED event word + cross-process FUTEX_WAKEs it, but cannot poke skmgr's local /dev/events
 * pipe — so skmgr, blocked in ppoll()/select() on that pipe, never wakes and never services Q_SkMgr.
 * (The libplibpp FThread waits via ppoll on the X fd + /dev/events; a bare timeout-cap can't help
 * because the EVHandler only runs Ev_receive when /dev/events is actually READABLE.)
 * Mirror the real kernel — whose /dev/events driver signals the fd on ANY Ev_sendtcb, cross-process:
 * spawn ONE lightweight watcher thread per registered evdev that futex-waits on its task's shared event
 * word and reconciles the LOCAL pipe on every change. A cross-process ev_send FUTEX_WAKEs the word ->
 * the watcher wakes -> pokes the local pipe -> the target's ppoll/select returns -> the FThread runs
 * Ev_receive and reads the queue. Idle cost is one blocked futex per GUI thread. Gated HEROS_EVDEV_WATCH
 * (default ON; =0 disables). */
static void *evdev_watcher_fn(void *arg){
    struct evdev *e=(struct evdev*)arg;
    int s=task_slot(e->task); if(s<0) return 0;
    volatile uint32_t *ev=&C->tasks[s].events;
    for(;;){
        uint32_t cur=__atomic_load_n(ev,__ATOMIC_ACQUIRE);
        lock(); evdev_reconcile_locked(e); unlock();
        futex(ev,FUTEX_WAIT,(int)cur,0);   /* block until ANY ev_send to this task (incl. cross-process) */
    }
    return 0;
}
/* Use pthread_create (NOT raw clone): under FEX the new thread needs its JIT/TLS context set up via
 * the intercepted pthread path — a raw clone() child bypasses that and dies immediately. Modern i386
 * glibc folds pthread into libc, so the symbol resolves at runtime without -lpthread. */
static void evdev_start_watcher(struct evdev *e){
    static int on=-2; if(on==-2){ const char*s=getenv("HEROS_EVDEV_WATCH"); on=(s&&s[0]=='0')?0:1; }
    if(!on) return;
    pthread_t th; pthread_attr_t at;
    pthread_attr_init(&at); pthread_attr_setdetachstate(&at,PTHREAD_CREATE_DETACHED);
    int rc=pthread_create(&th,&at,evdev_watcher_fn,e);
    pthread_attr_destroy(&at);
    if(rc!=0){ LOG("evdev watcher: pthread_create failed rc=%d\n",rc); return; }
    LOG("evdev watcher thread started for task 0x%x (cross-process /dev/events wake)\n",e->task);
}

/* ---------------- events ---------------- */
/* monotonic clock (i386 SYS_clock_gettime=265, CLOCK_MONOTONIC=1) for honoring
 * finite timeouts across spurious wakeups. i386 timespec = two 32-bit longs. */
static void mono_now(struct timespec *ts){ ts->tv_sec=0; ts->tv_nsec=0; raw5(265,1,(long)ts,0,0,0); }

/* cond: 1 = wait for ALL wanted bits, 2 = wait for ANY wanted bit. timeout in ms
 * (0=no wait, 0xffffffff=forever). Returns the caught event word; clears caught.
 * A finite-timeout Ev_receive is a BLOCKING wait, not a poll (pollers pass
 * timeout=0) — so it must sleep the FULL timeout across spurious futex wakeups /
 * EINTR (the As_send→SIGUSR1 path interrupts futex). Returning after a single
 * disturbed futex turned a 100s wait into a busy-spin (422MB log of one caller). */
/* HEROSCALL_EV_UNBLOCK_MS=N: DIRECT EVENT-INJECTION probe. A thread that would block FOREVER
 * (timeout=0xffffffff) with no event pending is instead woken after N ms and handed back its full
 * `want` mask as a SYNTHETIC event — i.e. "pretend the awaited event(s) fired". Tests whether
 * AppStartMP's pre-spawn block (task 0x106 Ev_receive(0x01019007, forever)) is a missing-event gate
 * (then it proceeds to spawn) or a missing-DATA gate (then it wakes, finds empty queues, re-blocks =
 * the config-data #6 frontier). Gated off by default. */
static int ev_unblock_ms=-2;
static volatile int runup_done=0;        /* set when run-up complete (HWS stub OR serve-loop fallback) — fwd-declared here for ev_receive */
static void post_inject_reread(void);    /* posts the synthetic UpdNewState that drives the full config-data load (defined after q_send) */
/* HEROSCALL_EV_INJECT_WANT / _BIT: TARGETED event injection. Only the forever-wait whose want-mask
 * == EV_INJECT_WANT gets the synthetic unblock (others stay forever, undisturbed), and it returns
 * ONLY (want & EV_INJECT_BIT) instead of the full want — so a single precise waitable bit is
 * delivered (avoids FWaitableInput::Unmask "0<mask" from over-notifying unarmed waitables).
 * Use for AppStartMP's pre-spawn Ev_receive(0x01019007): inject the CREATE_VOID_SUBSYSTEM bit. */
static uint32_t ev_inject_want=0, ev_inject_bit=0; static int ev_inject_init=0;
static uint32_t ev_receive(uint32_t want,uint32_t cond,uint32_t timeout){
    uint32_t self=task_self(); int s=task_slot(self);
    if(s<0) return 0;
    volatile uint32_t *ev=&C->tasks[s].events;
    C->tasks[s].last_ev_want=want;                 /* record for queue-notify bit matching */
    /* RUN-UP FALLBACK: the HWS-stub run-up-done signal does NOT fire on the path where ConfigServer
     * reaches its FModule serve loop directly (Ev_receive(0x01011000, forever) — the dispatch waiting on
     * its CfgServerQueue 0x01000000 bit + queue/event bits). Without runup_done, the INJECT_REREAD that
     * triggers the config-DATA load (ReadConfigDataSet → ReadDataFiles) never fires, so data-opens=0 and
     * ConfigServer never serves real config to a client (HrMmi). Treat reaching that forever serve-loop
     * wait as run-up-complete. (Process-local; harmless for non-ConfigServer procs.) */
    if(!runup_done && timeout==0xffffffff && (want & 0x01010000u)==0x01010000u){  /* forever serve-loop wait (own 0x10000 + CfgServerQueue 0x01000000) */
        __atomic_store_n(&runup_done,1,__ATOMIC_RELEASE);
        fprintf(stderr,"[rtos] RUNUP_COMPLETE pid=%d (serve-loop fallback; no HWS stub)\n",(int)getpid()); fflush(stderr);
        post_inject_reread();   /* drive the full config-DATA load (tnc.cfg/channel.cfg) on this path too */
    }
    /* HEROSCALL_EV_FORCE_TASK/_BIT: for one specific task, make any Ev_receive that wants the
     * force-bit return it immediately (no block) — breaks the FModule startup-handshake deadlock
     * where the logo thread (0x108) waits forever for the 0x1000 "input-ready"/ack from
     * AppStartMaster, which stops sending it. Forces the logo to spin its loop + drain its queue. */
    { static int fi=0; static uint32_t ft=0,fb=0;
      if(!fi){ fi=1; const char*t=getenv("HEROSCALL_EV_FORCE_TASK"); ft=t?(uint32_t)strtoul(t,0,16):0;
               const char*b=getenv("HEROSCALL_EV_FORCE_BIT");  fb=b?(uint32_t)strtoul(b,0,16):0; }
      if(ft && self==ft && fb && (want&fb)){ __atomic_and_fetch(ev,~(want&fb),__ATOMIC_ACQ_REL); return want&fb; } }
    /* (queue-pending heuristic removed: it falsely delivered a single-bit wait to ConfigServer's
     * task 0x100 — which waits 0x00080000 for a non-queue reason — corrupting its run-up.) */
    int synthetic=0;
    if(ev_unblock_ms==-2){ const char*e=getenv("HEROSCALL_EV_UNBLOCK_MS"); ev_unblock_ms=e?atoi(e):-1; }
    if(!ev_inject_init){ ev_inject_init=1;
        const char*w=getenv("HEROSCALL_EV_INJECT_WANT"); ev_inject_want=w?(uint32_t)strtoul(w,0,16):0;
        const char*b=getenv("HEROSCALL_EV_INJECT_BIT");  ev_inject_bit =b?(uint32_t)strtoul(b,0,16):0; }
    if(timeout==0xffffffff && ev_unblock_ms>0){
        if(!ev_inject_want || want==ev_inject_want){ timeout=(uint32_t)ev_unblock_ms; synthetic=1; }
    }
    /* HEROSCALL_EV_TIMEOUT_CAP_MS: cap LONG finite Ev_receive timeouts (e.g. the logo's 100s 0x1000
     * wait, 0x186a0ms) to a small value so a timeout-driven handshake/poll loop iterates fast enough
     * to progress within a bounded run instead of crawling. Only caps timeouts > 10s. */
    { static int evcap=-2; if(evcap==-2){ const char*e=getenv("HEROSCALL_EV_TIMEOUT_CAP_MS"); evcap=e?atoi(e):0; }
      if(evcap>0 && timeout>10000 && timeout!=0xffffffff) timeout=(uint32_t)evcap; }
    struct timespec deadline; int have_dl=0;
    if(timeout && timeout!=0xffffffff){
        mono_now(&deadline);
        deadline.tv_sec += timeout/1000; deadline.tv_nsec += (long)(timeout%1000)*1000000L;
        if(deadline.tv_nsec>=1000000000L){ deadline.tv_sec++; deadline.tv_nsec-=1000000000L; }
        have_dl=1;
    }
    /* DIRECT elapsed-based injection: once EV_INJECT_WANT has been awaited for > EV_UNBLOCK_MS
     * (tracked ACROSS calls, since the Monitor returns early on real logo events and never reaches
     * a single long wait), return ONLY (want & EV_INJECT_BIT) — the precise synthetic spawn event. */
    static struct timespec inj_t0; static int inj_armed=0;
    if(ev_inject_want && want==ev_inject_want && ev_inject_bit && ev_unblock_ms>0){
        struct timespec now; mono_now(&now);
        if(!inj_armed){ inj_armed=1; inj_t0=now; }
        long el=(now.tv_sec-inj_t0.tv_sec)*1000L+(now.tv_nsec-inj_t0.tv_nsec)/1000000L;
        if(el>ev_unblock_ms){ uint32_t r=want&ev_inject_bit;
            LOG("EV_INJECT: task 0x%x want %08x elapsed %ldms -> %08x\n",self,want,el,r); return r; }
    }
    int hst_waited=0;
    for(;;){
        /* ORPHAN main-context t_create rendezvous: a child registered with parent=0xffffffff (its
         * 0x80000 wake had no valid target). If we are a 0x80000-waiter (the t_create parent) and a
         * token is pending, consume it and deliver 0x80000 — completes the rendezvous so t_create
         * returns the (already-written) task id instead of -1. (See case 0x00.) */
        if(want & 0x80000u){
            uint32_t o=__atomic_load_n(&C->orphan_tc,__ATOMIC_ACQUIRE);
            while(o>0 && !__atomic_compare_exchange_n(&C->orphan_tc,&o,o-1,0,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)){}
            if(o>0){ LOG("EV orphan-rendezvous: task 0x%x want %08x -> delivering 0x80000\n",self,want);
                     return want & 0x80000u; }
        }
        uint32_t cur=__atomic_load_n(ev,__ATOMIC_ACQUIRE);
        int ok = cond==1 ? ((cur&want)==want) : ((cur&want)!=0);
        if(ok){
            uint32_t caught = cur & want;
            if(s>=0&&s<MAXTASK) C->tasks[s].nowait_miss=0;     /* EV_NOWAIT_YIELD: a real catch resets the spin streak */
            __atomic_and_fetch(ev,~caught,__ATOMIC_ACQ_REL);  /* consume */
            if(hstrace && (hst_waited||caught)) HST(self,0,"EV< t%x want=%08x c=%u -> caught %08x%s\n",
                self,want,cond,caught,hst_waited?" (woke)":"");
            evdev_reconcile(self);    /* events consumed -> re-arm/drain /dev/events to match */
            return caught;
        }
        if(timeout==0){ evdev_reconcile(self); /* poll miss -> drain stale signal */
            /* EV_NOWAIT_YIELD: a client thread that busy-spins Ev_receive(want, EV_NOWAIT) waiting for a
             * USEREVMASK "reply-available" bit (e.g. Guppy's softkey thread polling bit 0x8 while another
             * task — the 0x320 reply-queue reader t108 — must read+process the SkMgr reply and ev_send 0x8)
             * pegs a core. Under FEX (per-task host threads, contended VM) the spin can starve the reader
             * task so it never wakes on the cross-process queue notify -> the bit is never set -> livelock.
             * Throttle the spinning NOWAIT miss with a short usleep so the OS scheduler runs the reader.
             * Faithful: a NOWAIT poll returns "nothing" either way; only the spin rate changes (the real
             * kernel's scheduler/event coupling never spins this tight). Per-task consecutive-miss gated so
             * legit one-off polls aren't slowed. */
            static int evy=-1; static long evy_n=0;
            if(evy<0){ const char*e=getenv("HEROSCALL_EV_NOWAIT_YIELD"); evy=e?atoi(e):0;
                       const char*t=getenv("HEROSCALL_EV_NOWAIT_YIELD_N"); evy_n=t?atol(t):64; }
            if(evy>0 && s>=0 && s<MAXTASK){
                if(C->tasks[s].nowait_miss < 0x7fffffff) C->tasks[s].nowait_miss++;
                if(C->tasks[s].nowait_miss > evy_n) usleep((useconds_t)evy);
            }
            return 0; }
        /* HEROSCALL_SYSEVENT_AUTOFIRE=1: the reserved SYSEVENT bits (16-23, mask 0x00ff0000) are set
         * by the real kernel's Ev_sendtcb when a hardware/system sysevent fires (timer, X readable,
         * input). The emulator generates no such sysevents, so a thread that BLOCKS on a sysevent bit
         * (e.g. winmgr's logo thread Ev_receive(0x00011004) / main Ev_receive(0x05011001), bit 16)
         * waits forever -> the FModule logo-render handshake deadlocks BEFORE winmgr serves Q_WMGR.
         * Emulate the sysevent firing: when a forever-blocking Ev_receive wants a sysevent bit, set
         * those wanted sysevent bits in this task's own event word so the next loop iteration catches
         * them (the FModule code then proceeds as if the sysevent arrived). Gated (winmgr-only env). */
        { static int sysfire=-1; static uint32_t firemask=0; static long firelimit=-1;
          if(sysfire<0){ const char*e=getenv("HEROSCALL_SYSEVENT_AUTOFIRE"); sysfire=e&&e[0]=='1';
              const char*m=getenv("HEROSCALL_SYSEVENT_FIRE_MASK"); firemask=m?(uint32_t)strtoul(m,0,16):0x00ff0000u; }
          if(firelimit<0){ const char*l=getenv("HEROSCALL_SYSEVENT_FIRE_LIMIT"); firelimit=l?atol(l):0; }
          /* FIRE_LIMIT (per task): unlimited autofire makes a render thread that does
           * Ev_receive(forever, sysevent) in a tight loop busy-SPIN (819K fires) — it clears its OWN
           * waits but STARVES the sibling Q_WMGR serve thread (CPU + lock contention) so winmgr never
           * answers skmgr. With a cap, the task fires enough to get PAST its startup render-waits (create
           * windows) then BLOCKS normally on the next forever-wait, freeing the serve thread (woken by a
           * real queue notify). 0 = unlimited (back-compat). */
          if(sysfire && timeout==0xffffffff && firelimit>0 && s>=0 && s<MAXTASK
             && C->tasks[s].autofire_n >= (uint32_t)firelimit){
              /* cap reached for this task: fall through to the normal futex block (no fire) */
          } else
          if(sysfire && timeout==0xffffffff){
              /* Fire ONLY the sysevent bits in firemask (default all 16-23). Firing a thread's logic-
               * coupling sysevent (e.g. main's "logo-done" 0x00080000) directly makes it skip the step
               * that produces that signal (T_starting + waiting for the logo thread) -> a phantom path.
               * Restrict to the RENDER-side bit (0x00010000) so the LOGO thread proceeds + signals the
               * coupling bit to its peer NATURALLY. */
              /* NEVER autofire bit 19 (0x00080000): it is the libheros t_create<->t_start rendezvous
               * wake bit, not a real sysevent. Autofiring it phantom-wakes a t_create parent's
               * ev_receive(0x80000) BEFORE the child registers + writes the task id -> t_create
               * returns garbage/-1 ("Operating system could not create thread"). */
              uint32_t sysbits = want & 0x00ff0000u & firemask & ~0x00080000u;
              if(sysbits && (cur & sysbits)==0){
                  if(s>=0&&s<MAXTASK) C->tasks[s].autofire_n++;
                  __atomic_or_fetch(ev,sysbits,__ATOMIC_ACQ_REL);
                  continue;                       /* re-check: now catches the fired sysevent bit(s) */
              }
          }
        }
        if(hstrace && !hst_waited){ hst_waited=1; HST(self,0,"EV< t%x WAIT want=%08x c=%u to=%s (have=%08x)\n",
            self,want,cond,timeout==0xffffffff?"inf":"fin",cur); }
        struct timespec rel,*tp=0;
        if(have_dl){                                          /* compute the remaining slice */
            struct timespec now; mono_now(&now);
            long rem=(deadline.tv_sec-now.tv_sec)*1000L+(deadline.tv_nsec-now.tv_nsec)/1000000L;
            if(rem<=0){                                       /* genuinely timed out */
                if(synthetic){ uint32_t r = ev_inject_bit ? (want & ev_inject_bit) : want;
                    LOG("EV_UNBLOCK: synth want %08x inject %08x -> task 0x%x\n",want,r,self); return r; }
                return __atomic_load_n(ev,__ATOMIC_ACQUIRE)&want;
            }
            rel.tv_sec=rem/1000; rel.tv_nsec=(rem%1000)*1000000L; tp=&rel;
        }
        futex(ev,FUTEX_WAIT,cur,tp);                          /* forever (tp=0) or remaining slice */
    }
}
static int ev_send(uint32_t task,uint32_t bits){
    int s=task_slot(task); if(s<0) return -7;
    if(hstrace){ uint32_t snd=task_self();
        HST(snd,task,"EV> t%x -> t%x bits=%08x (tgt-wait=%08x)\n",snd,task,bits,C->tasks[s].last_ev_want); }
    __atomic_or_fetch(&C->tasks[s].events,bits,__ATOMIC_ACQ_REL);
    futex(&C->tasks[s].events,FUTEX_WAKE,0x7fffffff,0);
    evdev_reconcile(task);            /* wake a select()-blocked EVHandler on /dev/events */
    return 0;
}

/* ---------------- async signals (pSOS "asr") ----------------
 * Per-task pending word (as_pending) + enabled mask (as_mask). As_send ORs bits
 * into a target's pending; if any newly-raised bit is enabled (mask | forced),
 * the kernel delivers SIGUSR1 (10) to the target's OS thread (+ sig 18 for the
 * 0x400000 bit) so its ASR runs and calls As_read to drain. The forced-on bits
 * (0x2c00000 for send/read, 0xc00000 for mask) are always enabled. ABI recovered
 * from heros.ko {As_send,As_mask,As_read} + libheros {as_send,as_mask,as_catch}. */
#define AS_FORCED       0x2c00000u   /* always-enabled bits (As_send / As_read)        */
#define AS_FORCED_MASK  0x00c00000u  /* As_mask "newly-enabled & pending" trigger bits  */
static int as_deliver=1;             /* HEROSCALL_AS_DELIVER=0 disables real signalling  */
static int q_autocreate=1;           /* HEROSCALL_AUTO_QUEUE=0 disables black-hole queues */
static int qident_notify=-1;         /* HEROS_QIDENT_NOTIFY=0 disables compound "<mailslot>.<notifyqueue>" resolution */
static uint32_t qread_maxwait=0;     /* HEROSCALL_QREAD_MAXWAIT=ms caps "forever" Q_read waits (debug) */
static uint32_t sync_timeout=0;      /* HEROSCALL_SYNC_TIMEOUT=ms caps forever Q_read on "*Sync" handshake
                                      * queues only (e.g. QSikSync) — these deadlock when their server peer
                                      * isn't running (no external SikServer); legit fast replies still pass */
static int sem_autocount=1;          /* HEROSCALL_SEM_INIT=n: initial count for auto-created sems */
static int qdump=0;                  /* HEROSCALL_DUMPQ=1: hex-dump queue message payloads */
/* HEROS_EVT_RELAY=<queue>: with no real evtserver, ConfigServer's config broadcasts to QEvtServer (0x307)
 * reach a black hole and HrMmi never gets config -> blocks at Ev_receive(0x03011001). Forward every
 * QEvtServer message to the named subscriber queue (e.g. "QueueHrMmi") so HrMmi wakes + receives config. */
static const char*evt_relay_target=0; static int evt_relay_init=-1; static int in_evt_relay=0;
/* HEROSCALL_INJECT_WINMGR=1: AppStartMP registers the batch subsystems but never kicks off the first
 * subsystem START (no FmProcessState(state=2) for winmgr, no NEXT_CHILDSTAT). The boot only sees a
 * SPURIOUS FmProcessState for the pre-launched ConfigServer's heros name "cfgserver" (which doesn't
 * match a registered subsystem -> "Cannot track unknown process"). Inject a synthetic
 * FmProcessState(name="<NAME>", state=2) onto the AppStartMaster queue so Monitor::OnMessage
 * (state==2 && action==0 && pending==1) emits FmSubsystemAction(1=start) -> AppStart::Processes forks
 * winmgr. Wire = the captured FmProcessState template; HEROSCALL_INJECT_WINMGR_NAME overrides the name. */
static int inject_winmgr=-1, winmgr_injected=0, in_winmgr_inject=0;
static void put32(unsigned char*b,uint32_t v);   /* fwd-decl (defined in the INJECT_ACK section) */
/* HEROSCALL_INJECT_FMLOAD=1: construct + inject the constellation messages onto the AppStartMaster queue,
 * bypassing the stalled logo handshake. The chain forwards them: a flat FmLoadProcess (type 0xc80161)
 * traverses Monitor->Procedures->Config->Subsystems->Processes::OnMessage(FmLoadProcess)@0x4f650 which
 * FORKS the image directly via AppStart::Platform::PCreate (IsAFile-checked). FmSubsystemAction(action=0,
 * type 0xca0060) registers a subsystem in AppStart::Impl::subsystems first. GMessage wire format (proven
 * by INJECT_ACK/UPD): dword[0]=type-id, then per schema attribute a 4-byte tag: present = 0x000000<code>
 * (+payload), absent = 0x80000000|<code>. Codes: GMsgString=0xe7, GMsgInt=0x63, GMsgList=0x18c,
 * FM_SUBSYSTEM_TYPE(enum)=0x12. Schemas (RE'd 2026-06-24, scratchpad/spawn_inject_findings.md):
 *  FmLoadProcess[7]: processName(str), commandLineOptions(str), ifDefined(list), forEach(str),
 *                    imagePath(str), FM_SUBSYSTEM_TYPE(enum), GMsgInt. */
static int inject_fmload=-1, fmload_injected=0, in_fmload_inject=0;
static void inject_fmload_set(uint32_t qid);     /* fwd-decl (defined after put32) */
static int hws_stub=0;               /* HEROSCALL_HWS_STUB=1: auto-reply to QHWServer GetData requests
                                      * (the host-side IOsim has no i386 binary) — experimental */
static int timers_fire=0;            /* HEROSCALL_TIMERS=1: make Tm_evafter/Tm_evevery actually fire
                                      * their event (else the pending-client-msg flush timer never
                                      * fires → ConfigServer never sends connect-ACKs) */
/* HEROSCALL_REPLAY_TRIGGER=1: capture+replay CfgServerQueue startup self-messages, hoping to
 * re-fire CfgServer::SendConnected (the connect-ACK flush) for a post-startup client (IPO).
 * RESULT: DOES NOT WORK — verified the captured CfgServerQueue msgs (the only 3: post-to-anon-q
 * + QEvtServer register) are NOT the SendConnected trigger. The whole connect-ACK flow is
 * INTERNAL: SendConnected is driven by the SIK thread's flow (capped QSikSync read →
 * SikReadingFinished) and the startup clients are in-process EditThread sessions ACKed
 * internally — none is a queue message. So there is nothing to capture/echo. IPO (first
 * EXTERNAL client) needs the absent Qt MMI's UpdNewState, which must be CONSTRUCTED (no real
 * one exists to echo). Kept gated-off as a documented dead-end + reusable record/replay scaffold. */
static int replay_trigger=0;
/* runup_done declared earlier (before ev_receive) */
static volatile int trigger_replayed=0;
/* HEROSCALL_INJECT_UPD=1: after IPO's connect is read+registered, inject a synthetic UpdNewState
 * GMessage (id 0x1f0320, dispatch je@0x236091→OnUpdNewState) onto CfgServerQueue, to make
 * OnUpdNewState → ReadConfigDataSet + SendConnected flush IPO's pending ACK (impersonate the MMI).
 * v1 (header + 1 GMsgString) CRASHED the schema-driven deserializer (too few fields → read past).
 * v2 (header + 1 GMsgString "Nc" + 11 markers to match UpdNewState's ~12-field schema, types from
 * .rodata@0x232040) WORKS: ConfigServer deserializes it, OnUpdNewState runs — extracts "Nc" and does
 * ReadConfigDataSet (541B QEvtServer config broadcast) → SendConnected. So the MMI-impersonation
 * trigger is SOLVED. Remaining gap (separate): IPO isn't a REGISTERED pending client, so SendConnected
 * has nothing to flush to IPO (the GetClient-NULL / OnConnectClient registration issue). */
static int inject_upd=0;
/* HEROSCALL_INJECT_ACK=1: directly synthesize IPO's connect-ACK and post it to IPO's reply queue,
 * bypassing ConfigServer's SendConnected (which can't flush IPO — clients are registered only in
 * CfgServer::Initialize@0x187b4a, never in OnConnectClient, so IPO is never in the client map).
 * IPO sends CfgConnectClient(id 0x1700c0) carrying its reply-queue name as the first GMsgString,
 * then blocks reading that queue. We parse the name, build CfgClientIsConnected(id 0x170100;
 * fields clientId:GMsgString, id:0x63, success:CfgConnectResponse-enum 0x1700eb) with success=OK(0),
 * and post it. IPO's OnCfgClientIsConnected@0x1a72d0 reads success, and on OK proceeds to
 * CfgMailslotQueue::Create→SyncMessage→AskIpoConditions→IpoSystemView1 (past the connect). */
static int inject_ack=0;
/* HEROSCALL_INJECT_EVT_ACK=1: the SAME connect-ACK pattern as INJECT_ACK, but for the EVENT server
 * (QEvtServer). HrMmi's HrModule::ConnectToEvtSrv@0x2c140 sends EvtConnectClient(id 0x3200c0) to
 * QEvtServer with its reply-queue name as the leading GMsgString (idclient), then blocks reading that
 * queue. With no QEvtServer process the reply never comes → HrMmi blocks at Ev_receive(0x03011001)
 * AFTER receiving config + connecting to X (it never creates its window). We synthesize
 * EvtClientIsConnected(id 0x3200a0; 3 GMsgInt fields Success/stateError/viewerHandle, all 0 =
 * success) and post it to the reply queue → HrModule::OnEvtConnected@0x324e0 takes its success
 * branch (body+20==0) and continues the MMI bring-up. Schema RE'd from libGMessageMisc .rodata
 * 0x23ce80 (type id 0x3200a0 + 3×0x63) + HrModule::DispatchMessage (OnEvtConnected ← 3276960=0x3200a0). */
static int inject_evt_ack=0;
/* HEROSCALL_INJECT_PEER_ACK=1: the SAME connect-ACK pattern as INJECT_ACK/INJECT_EVT_ACK, but for the
 * OPERATIONAL-PEER constellation (IPO/NCK, PLC, ChannelManager). After config + the QEvtServer connect,
 * HrMmi's startup state machine (HrModule::MoveActiveStateTowardsTarget@0x33a60) issues connect/login
 * requests to its peers and BLOCKS at Ev_receive(0x03011001) until each is answered. The coordinator is a
 * REQUEST COUNTER at HrModule+59 (0xEC): each request `++`s it; each reply handler `--`s it via
 * HrModule::OneRequestDone@0x347c0, and when it hits 0 OneRequestDone calls MoveActiveStateTowardsTarget,
 * advancing the active state (Activate→SubscribeNcStart→…) toward UpdateDisplay (the render). Every peer
 * handler (OnIpoSrvConnected@0x35ca0 / OnPlcSrvConnected@0x35260 / OnCmGrantControl@0x35f50) calls
 * OneRequestDone in BOTH success and failure branches, but the SUCCESS branch (body+off==0) keeps the
 * target state high, so we want a zeroed reply. The reply messages are flat GMessage structs (QuickConstr4)
 * whose wire = [type-id][per-schema-field tag]; an ABSENT tag (0x80000000|code) leaves that field at its
 * default 0 in the (default-constructed/zeroed) body, so an all-absent reply deserializes to an all-zero
 * struct → the handler's success branch. Request→reply map + schemas RE'd from the libGMessage* .so schema
 * tables (the [type-id][field-codes…] arrays) + the captured request wire:
 *   IPO  req IpoSrvLogin     0x01a90040(->IPO 0x310)  => reply IpoSrvLoginQuit 0x41a90080
 *        schema [0x01a9006b,0x63,0xe7,0x63]           (libGMessageIpo @0x1d6230, body size 0x54)
 *   PLC  req PlcSrvLogin?     0x012f0160(->Q_PLC..0x30f)=> reply PlcSrvConnected 0x012f0180
 *        schema [0x012f0024,0x012f006b,0x84]          (libGMessagePlc @0x05b08c)
 *   CM   req CmConnect?       0x03340040(->CM 0x311)   => reply CmGrantControl  0x41cc05e1
 *        schema [0x01c20503,0x01c20503,0x01cc058b,0x01cc058b,0x01ad,0xc6] (libGMessageGeo @0x243d8c)
 * The reply-to is the request's leading GMsgString (".QueueHrMmi", same as INJECT_ACK) → QueueHrMmi 0x30e
 * (the queue HrMmi waits on; its notify bit 0x02000000 is in the 0x03011001 wait mask). */
static int inject_peer_ack=0;
/* HEROSCALL_INJECT_REREAD=1: ConfigServer loads its config DATA files (tnc.cfg → the "NC" channel
 * group etc.) only on a CfgRereadData message (id 0x170640 → CfgServer::OnRereadData@0x18f550 →
 * RereadData → ReadDataFiles). The MMI/constellation normally sends it at startup; standalone nobody
 * does, so the channel-group DB stays empty and IPO's -k=NC CheckOptions fails. We synthesize one and
 * post it to CfgServerQueue once ConfigServer's run-up completes (in ConfigServer's process, where the
 * HWS stub fires). Fields: clientId(GMsgString) id(0x63) copyDefaultData/silent/checkData/checkHome(bool). */
static int inject_reread=0;
static volatile int reread_injected=0;
#define CFGQ_CAP 128
#define CFGQ_MSG 1024
static struct { uint32_t len; uint8_t data[CFGQ_MSG]; } cfgq_rec[CFGQ_CAP];
static volatile int cfgq_n=0;
static void qhex(const char*tag,uint32_t id,const void*p,uint32_t n){
    if(!qdump||!p) return; if(n>1024)n=1024;
    fprintf(stderr,"   %s[0x%x]:",tag,id);
    for(uint32_t k=0;k<n;k++) fprintf(stderr,"%02x",((const uint8_t*)p)[k]); fprintf(stderr,"\n");
}
/* CAPTURE_MSG: dump the FULL bytes of the first message whose (header & 0x7fffffff) matches
 * HEROSCALL_CAPTURE_TYPE (hex) to /tmp/cap_<type>.bin — used to extract the real CfgActiveHandwheel
 * sub-message embedded in the 2711B HrMmiCfgGlobal (0x290081). Gated; one capture per type. */
static uint32_t capture_type=0; static int capture_done=0; static int capture_init=0;
static void capture_msg(const void*p,uint32_t n){
    if(!capture_init){ capture_init=1; const char*e=getenv("HEROSCALL_CAPTURE_TYPE"); capture_type=e?(uint32_t)strtoul(e,0,16):0; }
    if(!capture_type||capture_done||!p||n<4) return;
    uint32_t h=*(const uint32_t*)p & 0x7fffffff;
    if(h!=capture_type) return;
    char path[64]; snprintf(path,sizeof path,"/tmp/cap_%x.bin",capture_type);
    FILE*f=fopen(path,"wb"); if(f){ fwrite(p,1,n,f); fclose(f); capture_done=1;
        fprintf(stderr,"[rtos] CAPTURE_MSG: wrote %u bytes of type 0x%x to %s\n",n,capture_type,path); }
}

/* raise SIGUSR1 (and optionally 18) on the task's OS thread to invoke its ASR */
static void as_kick(int32_t tgid,int32_t tid,uint32_t trig){
    if(!as_deliver||tid<=0) return;
    raw5(SYS_tgkill,tgid,tid,10,0,0);                 /* SIGUSR1 = async-signal carrier */
    if(trig&0x400000) raw5(SYS_tgkill,tgid,tid,18,0,0);
}
static int as_send(uint32_t target,uint32_t bits){
    uint32_t tgt = (target==(uint32_t)-1) ? task_self() : target;
    int s=task_slot(tgt); if(s<0){ LOG("As_send unknown task 0x%x\n",tgt); return -9; }
    lock();
    uint32_t newbits = ~C->tasks[s].as_pending & bits;
    C->tasks[s].as_pending |= bits;
    uint32_t msk=C->tasks[s].as_mask; int32_t tgid=C->tasks[s].tgid,tid=C->tasks[s].tid;
    unlock();
    uint32_t trig = (msk|AS_FORCED) & newbits;
    LOG("As_send -> task 0x%x bits %08x (new %08x trig %08x)\n",tgt,bits,newbits,trig);
    if(trig) as_kick(tgid,tid,trig);
    return 0;
}
/* op: 0 = clear bits from mask, 1 = add bits to mask, 2 = set mask absolute.
 * Writes the resulting/old mask back through valp (the i386 caller reads it). */
static int as_mask_op(uint32_t *valp,uint32_t op){
    uint32_t self=task_self(); int s=task_slot(self); if(s<0) return -22;
    uint32_t in = valp?*valp:0, out, trig=0;
    int32_t tgid,tid;
    lock();
    uint32_t msk=C->tasks[s].as_mask, pend=C->tasks[s].as_pending;
    tgid=C->tasks[s].tgid; tid=C->tasks[s].tid;
    if(op==1){                                        /* add bits */
        if((~msk & in)==0) out=msk;                   /* all already set -> return old */
        else { out=msk|in; trig=((~msk & in)|AS_FORCED_MASK)&pend; C->tasks[s].as_mask=out; }
    } else if(op==2){                                 /* set absolute (as_enable) */
        out=msk; C->tasks[s].as_mask=in; trig=(in|AS_FORCED_MASK)&pend;  /* return OLD */
    } else {                                          /* op==0: clear bits */
        out=~in & msk; C->tasks[s].as_mask=out;
    }
    unlock();
    if(valp) *valp=out;
    if(trig) as_kick(tgid,tid,trig);                  /* a now-enabled signal is already pending */
    return 0;
}
/* As_read: caught = (req|forced) & pending; pending &= ~caught. req==0 means
 * "all of mask". Writes caught to outp, the (resolved) req back to reqp. */
static int as_read(uint32_t *reqp,uint32_t *outp){
    uint32_t self=task_self(); int s=task_slot(self); if(s<0) return -22;
    lock();
    uint32_t req = reqp?*reqp:0;
    if(req==0) req=C->tasks[s].as_mask;
    uint32_t caught=(req|AS_FORCED)&C->tasks[s].as_pending;
    C->tasks[s].as_pending &= ~caught;
    unlock();
    if(outp) *outp=caught;
    if(reqp) *reqp=req;
    LOG("As_read req %08x -> caught %08x\n",req,caught);
    return 0;
}

/* ---------------- semaphores ---------------- */
static uint32_t sem_make(const char*nm,int count){
    lock();
    if(nm&&nm[0]) for(int i=0;i<MAXSEM;i++) if(C->sems[i].used&&!strncmp(C->sems[i].name,nm,NAMELEN-1)){
        uint32_t id=C->sems[i].id; unlock(); return id; }
    int s=-1; for(int i=0;i<MAXSEM;i++) if(!C->sems[i].used){ s=i; break; }
    if(s<0){ unlock(); return 0; }
    C->sems[s].used=1; C->sems[s].id=C->next_sem++; C->sems[s].count=count;
    C->sems[s].name[0]=0; if(nm) strncpy(C->sems[s].name,nm,NAMELEN-1);
    uint32_t id=C->sems[s].id; unlock(); return id;
}
static int sem_slot(uint32_t id){ for(int i=0;i<MAXSEM;i++) if(C->sems[i].used&&C->sems[i].id==id) return i; return -1; }
static uint32_t sem_ident(const char*nm){
    lock();
    for(int i=0;i<MAXSEM;i++) if(C->sems[i].used&&!strncmp(C->sems[i].name,nm,NAMELEN-1)){
        uint32_t id=C->sems[i].id; unlock(); return id; }
    unlock();
    if(q_autocreate&&nm&&nm[0]){            /* peer-owned sem absent standalone: provide it
                                             * AVAILABLE (count 1) so the waiter proceeds */
        uint32_t id=sem_make(nm,sem_autocount); LOG("Sm_ident auto \"%s\" -> 0x%x (count %d)\n",nm,id,sem_autocount);
        return id;
    }
    return 0;
}
static int sem_request(uint32_t id,uint32_t timeout){      /* P / wait */
    int s=sem_slot(id); if(s<0) return -7;
    volatile int32_t *cnt=&C->sems[s].count;
    for(;;){
        int32_t c=__atomic_load_n(cnt,__ATOMIC_ACQUIRE);
        if(c>0){ if(__atomic_compare_exchange_n(cnt,&c,c-1,1,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return 0; continue; }
        if(timeout==0) return -0x3d;                       /* would block, nowait */
        struct timespec ts,*tp=0;
        if(timeout!=0xffffffff){ ts.tv_sec=timeout/1000; ts.tv_nsec=(timeout%1000)*1000000L; tp=&ts; }
        futex(cnt,FUTEX_WAIT,c,tp);
    }
}
static int sem_release(uint32_t id){                        /* V / signal */
    int s=sem_slot(id); if(s<0) return -7;
    __atomic_add_fetch(&C->sems[s].count,1,__ATOMIC_ACQ_REL);
    futex(&C->sems[s].count,FUTEX_WAKE,1,0);
    return 0;
}

/* ---------------- queues / mailslots ----------------
 * Queues are STRING-NAMED (Q_create registers a name, Q_ident finds it; shared
 * across processes via the segment). Messages are variable-length byte blobs
 * (the serialised GMessage), not fixed dword cells. ABI (heros.ko + libheros):
 *   Q_create  p[0]=name ptr, p[2]=depth, p[3]=flags          -> queue id
 *   Q_ident   p[0]=name ("queue" or "queue.process")          -> queue id / -0x13
 *   Q_send    p[0]=msg ptr,  p[2]=size,  p[4]=queue id         -> 0/err
 *   Q_read    p[0]=out buf,  p[2]=maxsize, p[7]=queue id, p[6]=timeout -> size  */
static int q_slot(uint32_t id){ for(int i=0;i<MAXQ;i++) if(C->queues[i].used&&C->queues[i].id==id) return i; return -1; }
/* copy a queue name, stripping any ".process" suffix; queues key on the base name */
static void q_basename(char*dst,const char*nm){
    int n=0; if(nm) while(nm[n]&&n<NAMELEN-1){ dst[n]=nm[n]; n++; } dst[n]=0;
    char*dot=strrchr(dst,'.'); if(dot) *dot=0;
}
static int q_find_slot(const char*base){
    for(int i=0;i<MAXQ;i++) if(C->queues[i].used&&!strncmp(C->queues[i].name,base,NAMELEN-1)) return i;
    return -1;
}
static int q_send(uint32_t id,const void*msg,uint32_t size,uint32_t mode);  /* fwd (q_create flush) */
static uint32_t q_create(const char*nm,uint32_t depth,uint32_t flags){
    char base[NAMELEN]; q_basename(base,nm);
    uint32_t owner=task_self();                          /* creator owns the queue (kernel +0xb8) */
    uint32_t nbits=(flags&2)?(flags&0xff000000u):0;      /* flag bit1 => notify; bits = top byte (kernel +0xe8) */
    lock();
    int s=q_find_slot(base);
    if(s>=0){                                            /* idempotent on name, BUT: */
        /* UPGRADE a no-notify PLACEHOLDER to its real notify owner. HeROS queues are global+named;
         * if a peer registered the name first as a no-notify OUTPUT queue (e.g. ConfigServer's
         * EvalContextOutQueue("AppStartMaster") create, which runs before AppStartMP exists), and the
         * REAL owner now re-creates it as a notify INPUT queue (EvalContextInQueue, flags&2), the
         * notify-registering owner must take it over — else the kernel notify (Ev_sendtcb +0xb8/+0xe8)
         * goes to the placeholder creator, and the real reader's dispatcher never wakes for its own
         * input messages (the AppStartMaster chain deadlock: posts notify ConfigServer, not AppStartMP). */
        if(nbits && !C->queues[s].notify_bits){
            C->queues[s].owner=owner; C->queues[s].notify_bits=nbits;
            LOG("Q_create UPGRADE \"%s\" placeholder -> owner 0x%x notify %08x\n",base,owner,nbits);
        }
        uint32_t id=C->queues[s].id; unlock(); return id;
    }
    s=-1; for(int i=0;i<MAXQ;i++) if(!C->queues[i].used){ s=i; break; }
    if(s<0){ unlock(); LOG("Q_create: table full\n"); return 0; }
    C->queues[s].used=1; C->queues[s].id=C->next_q++; C->queues[s].head=C->queues[s].tail=0;
    C->queues[s].depth=depth; C->queues[s].flags=flags;
    C->queues[s].owner=owner; C->queues[s].notify_bits=nbits;
    C->queues[s].name[0]=0; strncpy(C->queues[s].name,base,NAMELEN-1);
    uint32_t id=C->queues[s].id; unlock();
    LOG("Q_create \"%s\" depth %u flags %x owner 0x%x notify %08x -> 0x%x\n",base,depth,flags,owner,nbits,id);
    HST(owner,0,"QC \"%s\" id=%x owner=t%x flags=%x notify=%08x\n",base,id,owner,flags,nbits);
    /* QEvtServer late-subscriber FLUSH: when the relay's subscriber queue is created (HrMmi's
     * QueueHrMmi), deliver every QEvtServer broadcast buffered during ConfigServer's run-up to it,
     * so HrMmi receives the config events it subscribed to and wakes from Ev_receive(0x03011001). */
    if(evt_relay_init<0){ const char*e=getenv("HEROS_EVT_RELAY"); evt_relay_target=(e&&e[0])?e:0; evt_relay_init=1; }
    if(evt_relay_target && !in_evt_relay && !strcmp(base,evt_relay_target)){
        uint32_t n=C->evt_ring_n; if(n>EVTRING) n=EVTRING;
        if(n){ in_evt_relay=1;
            LOG("EVT_RELAY: FLUSH %u buffered QEvtServer broadcast(s) -> \"%s\" (0x%x)\n",n,base,id);
            uint32_t start = (C->evt_ring_n>EVTRING) ? (C->evt_ring_n-EVTRING) : 0;  /* oldest first (FIFO) */
            for(uint32_t i=start;i<C->evt_ring_n;i++){ uint32_t k=i%EVTRING;
                if(C->evt_ring[k].len) q_send(id,C->evt_ring[k].data,C->evt_ring[k].len,0); }
            in_evt_relay=0; }
    }
    return id;
}
/* Names used as service PRESENCE PROBES: auto-creating these defeats the control's
 * graceful "service absent" degradation. e.g. HeLogging::CheckHeloggerIsRunning does
 * q_ident("QueueHeLogger") and, on failure, logs locally instead of blocking on the
 * logger handshake (ConnectToHelogger). So these must report "not found". */
static int hr_ishex(char c){ return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F'); }
static int q_is_probe_name(const char*base){
    static const char*const probe[]={ "QueueHeLogger", 0 };
    for(int i=0;probe[i];i++) if(!strcmp(base,probe[i])) return 1;
    /* HwsMailslot transient per-instance mailslots ("HwsM<task>N<ctr>"): the control
     * SCANS these by incrementing the counter, stopping when Q_ident reports "not
     * found" (a peer server owns the real ones). Auto-creating each as a black hole
     * makes the scan never terminate — an unbounded loop (120MB+ log, starves peers
     * on the global lock). Report absent so the scan ends. */
    if(!strncmp(base,"HwsM",4)) return 1;
    /* PLC client notification-queue probes ("PLC<taskid>N<seq>", both hex, e.g.
     * PLC00000106N3dc): libPlcCtrl's PLC-client init (GdPlcCtrlPlcSrv*, PLCSRV_HANDLE)
     * enumerates the PLC server's per-client notify queues by scanning the sequence
     * until Q_ident reports "not found". With plc.elf absent NONE of these exist;
     * auto-creating each returns a fresh valid queue so the scan never terminates
     * (Guppy.elf looped 645991 idents, N000..N3de+, never reaching X). Report absent for
     * never-created PLC<hex>N<hex> names so the enumeration ends (the same bounded-scan
     * semantics as HwsM). A genuinely Q_create'd PLC queue is still found by q_find_slot,
     * which runs before this check, so this only suppresses the unbacked probe names. */
    if(base[0]=='P'&&base[1]=='L'&&base[2]=='C'){
        const char*p=base+3; const char*d=p;
        while(*p&&hr_ishex(*p)) p++;
        if(p>d && *p=='N'){ const char*e=++p;
            while(*p&&hr_ishex(*p)) p++;
            if(p>e && *p==0) return 1;
        }
    }
    return 0;
}
static uint32_t q_ident(const char*nm){
    char base[NAMELEN]; q_basename(base,nm);
    lock(); int s=q_find_slot(base); uint32_t id=(s>=0)?C->queues[s].id:0; unlock();
    /* "<oem>/mmi.qHF" = the MMI host-FRAME embed queue. GuppyRegisterWindow's NO_NCK_WINMGR
     * (no-window-manager) path PROBES it: present -> embed this X window into the host frame
     * (return 1 -> WndFocusPane falls back to non-bind-capable); ABSENT -> the window SELF-binds
     * its softkeys (return 0 -> WndFocusPane sets +0x14 and calls GUPPYSKMGR::Register). In the
     * 3-proc demo there is no main-MMI host frame, so auto-creating a black hole here makes the
     * window think it was embedded and it never self-binds. Report absent (the faithful no-host
     * behaviour) so Guppy self-registers its softkeys with skmgr. Same class as the HwsM/PLC*N*
     * presence-probe suppression. */
    if(!id && nm && strstr(nm,"mmi.qHF")){ LOG("Q_ident \"%s\" -> 0x0 (mmi.qHF host-frame absent)\n",nm); return 0; }
    /* COMPOUND reply-to "<mailslot>.<notifyqueue>" (HEROS_QIDENT_NOTIFY=1, default ON): HeROS
     * reply-to addresses embed the sender's config-mailslot identity BEFORE the dot and the
     * actual NOTIFY queue (the kernel queue the client created + reads) AFTER it, e.g.
     * "0000107CfgMc.Rtsffffffff". q_basename keeps the before-dot mailslot, which in the 3-proc
     * demo is UNBACKED (no per-client config-mailslot server) -> auto-create a black hole ->
     * a server's reply to that resolved id is LOST. Concretely: skmgr's SkMgrFrame::OnLogin ->
     * SkMgrClientManager::CreateClient does q_ident(loginPath="0000107CfgMc.Rtsffffffff") ->
     * SetQueueID, and Server::Connection::Flush q_sends the SkMgrLoginQuit there; with the
     * before-dot mailslot black-holed, the LoginQuit never reaches the client and its softkey
     * bind blocks forever. The real delivery target is the after-dot notify queue (the client
     * owns+reads it) — exactly what INJECT_ACK already extracts for the config connect-ack. So
     * when the before-dot base is ABSENT but the after-dot notify queue EXISTS, resolve to it.
     * Only fires for dotted names whose before-dot part is unbacked AND whose after-dot part is
     * a genuinely Q_create'd queue, so non-reply-to names and real before-dot queues are
     * unaffected (q_find_slot(base) already matched those above). */
    if(!id && qident_notify && nm && base[0]){    /* base[0]: only "<mailslot>.<notifyq>", NOT leading-dot ".X" (that is CFG_REPLY_ROUTE's empty-queue path) */
        const char*dot=strrchr(nm,'.');
        if(dot && dot>nm && dot[1]){
            lock(); int ns=q_find_slot(dot+1); uint32_t nid=(ns>=0)?C->queues[ns].id:0; unlock();
            if(nid){ LOG("Q_ident \"%s\" -> 0x%x (compound notify-queue \"%s\", mailslot \"%s\" unbacked)\n",
                         nm,nid,dot+1,base); return nid; }
        }
    }
    if(!id && q_autocreate && !q_is_probe_name(base)){    /* black-hole sink for absent peers */
        id=q_create(base,2,0); LOG("Q_ident \"%s\" -> auto 0x%x\n",base,id); return id;
    }
    LOG("Q_ident \"%s\" -> 0x%x\n",base,id);
    return id;                                            /* 0 => not found */
}
static int cfg_reply_route=-1;
static int sk_reply_route=-1;
static int q_send(uint32_t id,const void*msg,uint32_t size,uint32_t mode){
    int s=q_slot(id); if(s<0){ LOG("Q_send unknown queue 0x%x size %u\n",id,size); qhex("Q_FAIL",id,msg,size); return -9; }
    /* CFG_REPLY_ROUTE (HEROS_CFG_REPLY_ROUTE=1): ConfigServer resolves a per-client reply-to of ""
     * (the real connect-registration is bypassed by INJECT_ACK, so no Client reply-queue is recorded)
     * and Q_ident("")->the empty-named black-hole queue, then sends EVERY per-client reply there:
     * the connect-ack AND the config-DATA reply. Each reply, however, embeds its real reply-to as its
     * leading GMsgString (the clientId ".QueueHrMmi"/".EditThreadQue"/".EditThreadNotify"). Redirect a
     * send to the empty-named queue to the queue named by that leading string (strip the leading '.')
     * so the config data reaches the waiting client (QueueHrMmi 0x30e, notify 0x02000000). The
     * connect-ack (CfgClientIsConnected 0x170100) is left for INJECT_ACK so the client does not get a
     * duplicate ack ahead of the data reply. */
    if(cfg_reply_route<0){ const char*e=getenv("HEROS_CFG_REPLY_ROUTE"); cfg_reply_route=e&&e[0]=='1'; }
    if(cfg_reply_route && C->queues[s].name[0]==0 && msg && size>=12){
        const unsigned char*m=msg;
        uint32_t mtype=m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24);
        uint32_t tag  =m[4]|(m[5]<<8)|(m[6]<<16)|((uint32_t)m[7]<<24);
        if(tag==0xe7 && !(inject_ack && (mtype&0x7fffffff)==0x170100)){   /* leading GMsgString; skip connect-ack */
            uint32_t nlen=m[8]|(m[9]<<8)|(m[10]<<16)|((uint32_t)m[11]<<24);
            if(nlen>=1 && nlen<=64 && 12+nlen<=size){
                char nm[80]; memcpy(nm,m+12,nlen); nm[nlen]=0;
                const char*base=nm; if(base[0]=='.') base++;     /* ".QueueHrMmi" -> "QueueHrMmi" */
                int rs=q_find_slot(base);
                if(rs>=0 && rs!=s && C->queues[rs].name[0]){
                    LOG("CFG_REPLY_ROUTE: redirect \"\"(0x%x) -> \"%s\"(0x%x) by embedded reply-to \"%s\" (type %08x, %u bytes)\n",
                        C->queues[s].id,C->queues[rs].name,C->queues[rs].id,nm,mtype,size);
                    s=rs; id=C->queues[rs].id;
                }
            }
        }
    }
    /* SK_REPLY_ROUTE (HEROSCALL_SK_REPLY_ROUTE, default ON): skmgr replies to the softkey client's
     * per-process SYNCHRONOUS reply queue "Rts<X>" (the reply-to embedded in SkMgrLogin, e.g.
     * "0000107CfgMc.Rtsffffffff" -> Rtsffffffff/0x320). But the softkey API caller (the Python main
     * thread) reads its OWN "Client<X>" queue ("Clientffffffff"/0x31e, notify 0x02000000) — NOT the
     * shared "Rts<X>", which the per-process config DISPATCH thread owns/reads and is wedged in the
     * config-#6 GetData (so it never drains/routes the softkey replies -> SkMgrLoginQuit + the
     * SkMgrInfoResponses carrying the .bmx softkey-icon paths strand on 0x320 -> the bar never fills).
     * "Rts<suffix>" and "Client<suffix>" share the per-client <suffix>, so redirect a softkey-family
     * (type-id>>16 == 0x28a) reply from "Rts<suffix>" to "Client<suffix>" if that queue exists. Same
     * class as CFG_REPLY_ROUTE (redirect a reply to the queue the waiter actually reads). */
    if(sk_reply_route<0){ const char*e=getenv("HEROSCALL_SK_REPLY_ROUTE"); sk_reply_route=e?(e[0]=='1'):1; }
    if(sk_reply_route && msg && size>=4 && !strncmp(C->queues[s].name,"Rts",3)){
        const unsigned char*m=msg;
        uint32_t mtype=m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24);
        if((mtype>>16)==0x28au){
            char cn[NAMELEN]; snprintf(cn,sizeof cn,"Client%s",C->queues[s].name+3);  /* "Rtsffffffff"+3 -> "Clientffffffff" */
            int rs=q_find_slot(cn);
            if(rs>=0 && rs!=s && C->queues[rs].name[0]){
                LOG("SK_REPLY_ROUTE: redirect softkey reply \"%s\"(0x%x) -> \"%s\"(0x%x) (type %08x, %u bytes)\n",
                    C->queues[s].name,C->queues[s].id,cn,C->queues[rs].id,mtype,size);
                s=rs; id=C->queues[rs].id;
            }
        }
    }
    if(size>QMSGCAP){ LOG("Q_send size %u > cap %u, truncating\n",size,QMSGCAP); size=QMSGCAP; }
    uint32_t sender=task_self();
    struct queue*q=&C->queues[s];
    /* RTS_FAMILY_ROUTE: record this task's request family (type-id>>16) when it sends a REQUEST to a
     * server queue, so the matching reply can be routed to it on the SHARED ".Rts" per-process reply
     * queue (config requests = 0x17, softkey requests = 0x28a). Guppy multiplexes BOTH the config
     * dispatch thread and the Python softkey-login waiter onto one ".Rts" queue; without this the
     * config reader consumes the softkey replies (wrong family) before the softkey waiter can. */
    if(msg && size>=4){
        const char*qn=q->name;
        if(!strcmp(qn,"CfgServerQueue")||!strcmp(qn,"Q_SkMgr")||!strcmp(qn,"Q_SkMgrCtrl")){
            int ss=task_slot(sender);
            if(ss>=0) C->tasks[ss].last_req_family=(*(const uint32_t*)msg)>>16;
        }
    }
    lock();
    uint32_t used=q->tail-q->head;
    if(used>=QSLOTS) q->head++;                           /* drop oldest to make room */
    uint32_t slot=q->tail%QSLOTS;
    q->msg[slot].len=size;
    q->msg[slot].hdr[0]=id;                               /* source queue id (kernel node +0x20) */
    q->msg[slot].hdr[1]=sender;                           /* sender task    (kernel node +0x24) */
    q->msg[slot].hdr[2]=(mode&0xffff)|((size&0xffff)<<16);/* mode | size    (kernel node +0x28) */
    if(msg&&size) memcpy(q->msg[slot].data,msg,size);
    qhex("Q_send",id,msg,size);
    capture_msg(msg,size);
    __atomic_add_fetch(&q->tail,1,__ATOMIC_ACQ_REL);
    uint32_t owner=q->owner, nbits=q->notify_bits;
    /* (the AppStartMP notify-bit-match heuristic was reverted: it didn't crack the logo handshake
     * and risked perturbing ConfigServer's worker-thread run-up. Plain flags-byte notify, as in the
     * known-good run_2proc_cfgfix that served IPO.) */
    unlock();
    futex(&q->tail,FUTEX_WAKE,0x7fffffff,0);          /* wake any Q_read blocker (kernel __wake_up) */
    if(hstrace){ int os=task_slot(owner);
        uint32_t mtag = (msg && size>=4) ? *(const uint32_t*)msg : 0;          /* GMessage type id = 1st dword (LE) */
        HST(sender,owner,"QS [%x]\"%s\" size=%u tag=%08x sndr=t%x notify=%08x->t%x [%s]\n",
            id,q->name,size,mtag,sender,nbits,owner, msascii(msg,size)); }
    if(nbits&&owner) ev_send(owner,nbits);            /* event-driven serve loop (kernel Ev_sendtcb +0xb8/+0xe8) */
    LOG("Q_send -> queue 0x%x size %u (depth %u) notify %08x->task 0x%x\n",id,size,used+1,nbits,owner);
    /* INJECT_WINMGR: on the first FmProcessState (0x40c803e0) posted to the AppStartMaster queue
     * (= subsystems registered, the boot reached the process-state stage), inject a synthetic
     * FmProcessState(state=2) for the winmgr subsystem so its start fires. */
    if(inject_winmgr<0){ const char*e=getenv("HEROSCALL_INJECT_WINMGR"); inject_winmgr=e&&e[0]=='1'; }
    if(inject_winmgr && !winmgr_injected && !in_winmgr_inject && msg && size>=8
       && *(const uint32_t*)msg==0x40c803e0u && !strcmp(C->queues[s].name,"AppStartMaster")){
        winmgr_injected=1; in_winmgr_inject=1;
        const char*nm=getenv("HEROSCALL_INJECT_WINMGR_NAME"); if(!nm||!nm[0]) nm="winmgr:~/winmgr";
        unsigned char ps[52]; memset(ps,0,sizeof ps);
        put32(ps+0,0x40c803e0u);                      /* FmProcessState id */
        put32(ps+4,0x00000002u);                      /* state = 2 (to-start) */
        put32(ps+16,0x00010000u);                     /* flag (from the captured template) */
        size_t nl=strlen(nm); if(nl>30) nl=30; memcpy(ps+20,nm,nl);   /* name at body+16 (offset 20) */
        LOG("INJECT_WINMGR: posting synthetic FmProcessState(\"%s\",state=2) to AppStartMaster(0x%x)\n",nm,id);
        q_send(id,ps,sizeof ps,0);
        in_winmgr_inject=0;
    }
    /* INJECT_FMLOAD: on the first message posted to the AppStartMaster queue after the chain is up
     * (the spurious cfgserver FmProcessState, or any chain message), inject the constellation
     * FmSubsystemAction(register)+FmLoadProcess set so the chain forks the processes, bypassing the
     * stalled logo handshake. Triggered once. */
    if(inject_fmload<0){ const char*e=getenv("HEROSCALL_INJECT_FMLOAD"); inject_fmload=e&&e[0]=='1'; }
    if(inject_fmload && !fmload_injected && !in_fmload_inject && msg && size>=8
       && *(const uint32_t*)msg==0x40c803e0u && !strcmp(C->queues[s].name,"AppStartMaster")){
        fmload_injected=1; in_fmload_inject=1;
        fprintf(stderr,"[rtos] INJECT_FMLOAD: trigger seen on AppStartMaster(0x%x) -> injecting constellation set\n",id); fflush(stderr);
        inject_fmload_set(id);
        in_fmload_inject=0;
    }
    /* INJECT_WMGR_ACK: WM clients (skmgr, Guppy) connect to the window manager by sending WM requests to
     * Q_WMGR — 0x302c (StartTimer) + 0x3037 (GetScreens) — embedding their reply-queue id at msg+24 and a
     * seq at msg+4, then BLOCK reading that reply queue (WMQ<task>) for winmgr's reply. With no winmgr (or
     * winmgr stuck in its own logo-render handshake) the reply never comes and skmgr never reaches its
     * softkey serve loop, so a client's SkMgrLogin on Q_SkMgr is never processed. winmgr's
     * HandleMessage@winmgr.elf 0x29f00 answers 0x3037 with a 208-byte type-0x3037 screen-list reply.
     * FAITHFUL LAYOUT (RE'd from the decompile, the empty-screen else-branch @0x29f00:1159-1168; dest base
     * ebp-0x434): *(u32*)dest=12343(0x3037)@off0; v273=a1[1](=request seq@off4) written @REPLY off8 (NOT off4!);
     * v274/v275@off12/16=0 (screenNum/screenId); v276@off20=0 (name); v283@off68=0 (name2); v285@off196=0
     * (flag); v287(_BOOL4 is-last)@off204=TRUE. The client's WaitForExpectedMessage correlates on the seq at
     * REPLY off8 — the prior version wrote it at off4 (seq mismatch → discarded → skmgr spun) and never set
     * the is-last @off204. Reproduce winmgr's bytes exactly. Same INJECT_ACK class as Cfg/Evt/Peer. */
    { static int inject_wmgr_ack=-1;
      if(inject_wmgr_ack<0){ const char*e=getenv("HEROSCALL_INJECT_WMGR_ACK"); inject_wmgr_ack=e&&e[0]=='1'; }
      /* 0x3001 = WM CONNECT/RegisterClient (GuppyRuntimeGtk's WM-init, taken on the FAITHFUL gdk-internal
       * path that DISPLAY=:0.0 enables; the gate-forced gdk-external path skips it). winmgr HandleMessage@
       * 0x29f00 case 0x3001 builds a 16-byte reply: *(dest+0)=12289(0x3001); v273=a1[1](req seq)@dest+8;
       * v274=GetRootWindow(a1[4])@dest+12; v272=0@dest+4 — then SendReply(client,dest,16) / WmSendEvent(a1[5],
       * dest,16). The WM msgs are RAW dword structs (a1[i]=*(u32*)(msg+4i)); the reply target is a1[5]=msg+20
       * (the client's reply queue, Guppy's 0x31d). Without this, Guppy blocks reading 0x31d for the connect
       * reply BEFORE it creates its window / reaches softkey.Register, so the bar never renders even though the
       * softkey CONTENT routes fine (1904110). RootWindow defaults 0 (env HEROSCALL_WMGR_ROOT overrides w/ the
       * real Xvfb root id if Guppy validates/reparents). Same INJECT_ACK class as 0x3037 below. */
      if(inject_wmgr_ack && msg && size>=24 && !strcmp(C->queues[s].name,"Q_WMGR")
         && *(const uint32_t*)msg==0x3001u){
          uint32_t seq=*(const uint32_t*)((const char*)msg+4);
          uint32_t a10=*(const uint32_t*)((const char*)msg+8);     /* a1[10] = client's last-seen WM event serial */
          uint32_t rq =*(const uint32_t*)((const char*)msg+20);   /* a1[5] = reply queue (Guppy 0x31d) */
          if(rq && q_slot(rq)>=0){
              static uint32_t wroot=0xffffffffu;
              if(wroot==0xffffffffu){ const char*e=getenv("HEROSCALL_WMGR_ROOT"); wroot=e?(uint32_t)strtoul(e,0,0):0u; }
              unsigned char rep[16]; memset(rep,0,sizeof rep);
              put32(rep+0,0x3001u);   /* reply type 12289 = WM connect reply */
              put32(rep+4,a10+1);     /* off4 = WM EVENT SERIAL: WmSendRequestReply gap-checks serial-1==a1[10] */
              put32(rep+8,seq);       /* v273 = request seq @off8 (reqid; WmSendRequestReply checks ==request seq) */
              put32(rep+12,wroot);    /* v274 = RootWindow @off12 */
              LOG("INJECT_WMGR_ACK: posting 0x3001 connect-reply (seq %u@off8, serial %u@off4, root 0x%x, 16B) to WM reply-q 0x%x\n",seq,a10+1,wroot,rq);
              q_send(rq,rep,sizeof rep,0);
          }
      }
      if(inject_wmgr_ack && msg && size>=28 && !strcmp(C->queues[s].name,"Q_WMGR")
         && *(const uint32_t*)msg==0x3037u){
          uint32_t seq=*(const uint32_t*)((const char*)msg+4);
          uint32_t a10=*(const uint32_t*)((const char*)msg+8);     /* a1[10] = client's last-seen WM event serial */
          uint32_t rq =*(const uint32_t*)((const char*)msg+24);
          if(rq && q_slot(rq)>=0){
              unsigned char rep[208]; memset(rep,0,sizeof rep);
              put32(rep+0,0x3037u);   /* dest off0:  reply type 12343 = GetScreens reply */
              put32(rep+4,a10+1);     /* dest off4:  WM EVENT SERIAL (gap-checked serial-1==a1[10]); was 0 -> GAP after the connect's serial 0 -> WMGRErrSync */
              put32(rep+8,seq);       /* dest off8:  v273 = request seq (winmgr writes a1[1] HERE, not off4) */
              /* HEROSCALL_WMGR_SCREEN=1 -> the NON-EMPTY (do-while) branch: one valid screen so the WM
               * client has a screen to attach its softkey bar to (the demo HwViewer targets the OEM
               * screen; layout tnc640layout1280.xml: SCREEN_OEM name="OEM"). winmgr's do-while writes
               * v274=screenNum, v275=screenId, v276=name(0x30), v283=name2(0x80), v285=flag(ScreenId+124),
               * v287=is-last. =0 (default) -> the empty-screen else-branch. */
              static int scr=-1; if(scr<0){ const char*e=getenv("HEROSCALL_WMGR_SCREEN"); scr=e&&e[0]=='1'; }
              if(scr){ put32(rep+12,0u);            /* v274 screenNum */
                       put32(rep+16,2u);            /* v275 screenId (OEM desktopId 2) */
                       memcpy(rep+20,"OEM",4);      /* v276 name (48B field) */
                       memcpy(rep+68,"OEM",4);      /* v283 name2 (128B field) */
                       rep[196]=0;                  /* v285 flag byte (ScreenId+124) */
              }
              put32(rep+204,1u);      /* dest off204: v287 _BOOL4 is-last = TRUE (only/last screen) */
              LOG("INJECT_WMGR_ACK: posting 0x3037 screen-reply (seq %u@off8, serial %u@off4, %s, is-last, 208B) to WM reply-q 0x%x\n",seq,a10+1,scr?"1 screen OEM":"empty",rq);
              q_send(rq,rep,sizeof rep,0);
          }
      }
      /* 0x3042 = WM SYNC (WmSync@libwinmgrlib 0x86d0). The client sends a 24-byte request
       * [off0=12354, off4=seq, off8=a1[10], off12=a1[11], off16=pid, off20=tid] and blocks in
       * WmSendRequestReply reading its session queue a1[2] = "WMQ<tid>" for the reply. winmgr's
       * HandleMessage@0x29f00 case 0x3042 builds a WmEvent (SetType(10)+SetValue(seq)) and sends it
       * to the client via WmClient vtable+80 (-> WmSendEvent -> q_send of the serialized event).
       * The CLIENT only requires (WmSendRequestReply LABEL_34): reply off0<=12355, off4==a1[10]+1
       * (event serial, gap-checked), off8==request seq (reqid). The 24-byte sync request carries
       * NO reply-queue id (unlike 0x3001/0x3037) — the reply goes to a1[2]=WMQ<a1[1]=tid@off20>.
       * Without this reply WmSync never returns and Guppy's WndFullScreen WM-registration handshake
       * never completes, so the fullscreen geometry + window map (hence the softkey bar layout) never
       * happen. Same INJECT_ACK class as 0x3001/0x3037. */
      if(inject_wmgr_ack && msg && size>=24 && !strcmp(C->queues[s].name,"Q_WMGR")
         && *(const uint32_t*)msg==0x3042u){
          uint32_t seq=*(const uint32_t*)((const char*)msg+4);
          uint32_t a10=*(const uint32_t*)((const char*)msg+8);     /* a1[10] = client's last-seen WM event serial */
          uint32_t tid=*(const uint32_t*)((const char*)msg+20);    /* a1[1] = WM context task -> reply queue WMQ<tid> */
          char wmq[NAMELEN]; snprintf(wmq,sizeof wmq,"WMQ%05X",tid);
          int rs=q_find_slot(wmq); uint32_t rq=(rs>=0)?C->queues[rs].id:0;
          if(rq && q_slot(rq)>=0){
              /* The reply is a serialized WmEvent (type 10). The client discards the buffer after the
               * sync barrier; only off0/off4/off8 matter. Mirror the winmgr_event_ envelope used by
               * the 0x3001/0x3037 replies: off0=event type, off4=serial, off8=reqid. Size = 208 (the
               * dest WmEvent buffer class). off0=10 (WmEvent::Type 10, <=12355 -> LABEL_34 success). */
              unsigned char rep[208]; memset(rep,0,sizeof rep);
              put32(rep+0,10u);       /* off0: WmEvent type 10 (sync-reply); <=12355 -> normal event */
              put32(rep+4,a10+1);     /* off4: WM event serial (gap-checked serial-1==a1[10]) */
              put32(rep+8,seq);       /* off8: reqid (== request seq; WmSendRequestReply LABEL_34 checks v28==a4[2]) */
              LOG("INJECT_WMGR_ACK: posting 0x3042 sync-reply (seq %u@off8, serial %u@off4, type 10, 208B) to WM reply-q \"%s\" 0x%x\n",seq,a10+1,wmq,rq);
              q_send(rq,rep,sizeof rep,0);
          } else {
              LOG("INJECT_WMGR_ACK: 0x3042 sync — reply queue \"%s\" (tid 0x%x) not found, skipping\n",wmq,tid);
          }
      }
    }
    /* REPLAY_TRIGGER: record startup self-messages to CfgServerQueue (verbatim, valid bytes) */
    if(replay_trigger && !runup_done && msg && size && size<=CFGQ_MSG
       && !strcmp(C->queues[s].name,"CfgServerQueue")){
        int k=__atomic_fetch_add(&cfgq_n,1,__ATOMIC_ACQ_REL);
        if(k<CFGQ_CAP){ cfgq_rec[k].len=size; memcpy(cfgq_rec[k].data,msg,size); }
        else __atomic_store_n(&cfgq_n,CFGQ_CAP,__ATOMIC_RELEASE);
    }
    /* QEvtServer RELAY: emulate evtserver's fan-out so HrMmi gets config without a live (crashing)
     * evtserver. Every QEvtServer broadcast is BUFFERED in the shared evt_ring; if the subscriber queue
     * already exists it is also forwarded NOW; and q_create flushes the whole ring to the subscriber when
     * it appears (ConfigServer broadcasts during run-up, before HrMmi's queue exists). Target != QEvtServer
     * => no recursion. */
    if(evt_relay_init<0){ const char*e=getenv("HEROS_EVT_RELAY"); evt_relay_target=(e&&e[0])?e:0; evt_relay_init=1; }
    if(evt_relay_target && !in_evt_relay && msg && size && size<=EVTMSGCAP && !strcmp(C->queues[s].name,"QEvtServer")){
        lock();
        uint32_t k = C->evt_ring_n % EVTRING;            /* buffer the broadcast (shared, for late flush) */
        C->evt_ring[k].len = size; memcpy(C->evt_ring[k].data, msg, size); C->evt_ring_n++;
        int rt=q_find_slot(evt_relay_target); uint32_t rtid=(rt>=0)?C->queues[rt].id:0;
        unlock();
        if(rtid){ in_evt_relay=1;
            LOG("EVT_RELAY: forwarding QEvtServer msg (%u bytes, tag %08x) -> \"%s\" (0x%x)\n",
                size,(size>=4)?*(const uint32_t*)msg:0,evt_relay_target,rtid);
            q_send(rtid,msg,size,mode); in_evt_relay=0; }
    }
    return 0;
}
/* fwd-decls (defined in the INJECT_EVT_ERR section): release the deferred EvtAns when HrMmi reads
 * the HrMmiCfgGlobal (0x290081), so the active-state target is bootstrapped BEFORE the counter drains. */
static uint32_t deferred_evterr_rqid;
static void post_evt_ans_error(uint32_t rqid);
/* WMQ_BREAK per-queue consecutive-empty-no-wait-read counter (slot-indexed; reset on a real read). */
static unsigned long wmq_empty_streak[1024];
static int q_read(uint32_t id,void*buf,uint32_t maxsize,uint32_t timeout,uint32_t*hdrbuf){
    int s=q_slot(id); if(s<0){ LOG("Q_read unknown queue 0x%x\n",id); errno=EINVAL; return -9; }  /* errno!=ENOMEM: wrapper -> 0, not "grow" */
    if(timeout==0xffffffff && qread_maxwait) timeout=qread_maxwait;   /* debug: cap ALL forever-waits */
    if(timeout==0xffffffff && sync_timeout && strstr(C->queues[s].name,"Sync")){ /* cap only *Sync handshakes */
        /* NB: capping the HwsM* HWS reply mailslots does NOT help — the run-up's SyncMessage
         * re-reads the reply queue after each timeout (polls forever), it does not degrade on a
         * missing HW server. It needs a REAL reply injected (QHWServer stub). See docs/17. */
        timeout=sync_timeout; LOG("Q_read \"%s\" forever-wait capped to %ums (no server peer)\n",C->queues[s].name,sync_timeout); }
    struct queue*q=&C->queues[s];
    /* RTS_FAMILY_ROUTE — DEFAULT OFF (superseded by SK_REPLY_ROUTE). This modelled the shared "Rts<X>"
     * queue as having MULTIPLE per-family readers (config + softkey), each filtering by its request
     * family. RE refuted that: there is ONE per-process DISPATCH thread that reads "Rts<X>" and the
     * softkey API caller reads its OWN "Client<X>" queue (it never reads "Rts<X>"). So the correct fix
     * is to route softkey replies to "Client<X>" (SK_REPLY_ROUTE in q_send), not to filter "Rts<X>"
     * reads. Kept (gated off) for reference / A-B. */
    static int rts_family_route=-1;
    if(rts_family_route<0){ const char*e=getenv("HEROSCALL_RTS_FAMILY_ROUTE"); rts_family_route=e&&e[0]=='1'; }  /* default OFF */
    for(;;){
        lock();
        if(q->tail!=q->head){
            /* RTS_FAMILY_ROUTE: on a SHARED ".Rts" per-process reply queue, Guppy multiplexes BOTH its
             * config dispatch thread (reads 0x17xxxx replies) AND its Python softkey-login waiter (reads
             * 0x28a0xxx replies). A plain FIFO head read lets whichever thread wakes first dequeue the
             * other's reply (wrong family) and discard it -> the softkey login never completes -> no
             * RegisterConnection -> no GData fill -> the bar never draws. Faithful FModule dispatch routes
             * each reply to the waiter whose request-family matches; replicate that here: a reader on a
             * ".Rts" queue dequeues only the first message whose family (type-id>>16) == the family of the
             * request IT last sent, leaving other-family messages in place for the other waiter. */
            uint32_t pos=q->head; int have=1;                     /* default: FIFO head */
            /* the per-process synchronous reply queue is named "Rts<hex>" (e.g. "Rtsffffffff") and the
             * compound reply-to is "<mailslot>.Rts<hex>" — match "Rts" (NOT ".Rts"; the queue's own
             * registered name has no leading dot). No other HeROS queue name contains "Rts". */
            if(rts_family_route && strstr(C->queues[s].name,"Rts")){
                int rsl=task_slot(task_self());
                uint32_t fam=(rsl>=0)?C->tasks[rsl].last_req_family:0;
                if(fam){
                    have=0;
                    for(uint32_t p=q->head; p!=q->tail; p++){
                        uint32_t fsl=p%QSLOTS;
                        uint32_t ty=(q->msg[fsl].len>=4)?*(const uint32_t*)q->msg[fsl].data:0;
                        if((ty>>16)==fam){ pos=p; have=1; break; }
                    }
                    /* have==0: no message of my family yet -> fall through to the wait path (do NOT
                     * consume another waiter's reply). The futex wake on q->tail re-checks on every send. */
                }
            }
            if(have){
            uint32_t slot=pos%QSLOTS;
            uint32_t full=q->msg[slot].len;                       /* the REAL message length */
            /* TOO-BIG contract — FAITHFUL to heros.ko Q_read_ex @0x10c510 (decompiled): if the caller's
             * buffer is smaller than the message (maxsize=p[2] < msglen) the real kernel does NOT dequeue
             * and RETURNS 0xfffffff4 (=-12). HrMmi's FMailslotQueue::ReadMessageNoAlert/ReadQueue then
             * DOUBLES its buffer (128->256->512...) and re-reads until it fits (asm @0x21f60: `js` on the
             * NEGATIVE return -> GMsgStreamBase::allocate(cap*2) -> q_read again -> success when eax>0).
             * The OLD code returned the POSITIVE full size, so HrMmi's `js` (sign test) was NEVER taken:
             * it fell through to the success path and parsed the TRUNCATED 128B buffer -> GMessage::Read
             * abort "message error 0x2100018 at offset 0x80" (a GMsgString-field fread underflow at the
             * 128-byte boundary). ConfigServer is unaffected: it reads with a 0x8000 buffer (full<=maxsize). */
            if(maxsize && full>maxsize){
                unlock();
                /* The libheros.so q_read wrapper (@0xbfb0) maps the heroscall return to the value the
                 * FMailslotQueue receive checks: it ONLY treats the read as "too big, grow+re-read" when
                 * the syscall returns NEGATIVE *AND* errno==ENOMEM (12) (`jns`+`cmp [errno],0xc`->neg ->
                 * returns -1); any other negative -> 0 (= "no message"). So returning -12 alone is NOT
                 * enough — without errno==ENOMEM the wrapper returns 0 and the caller never grows
                 * (it deadlocks on the un-dequeued head message). Set errno=ENOMEM so the wrapper yields
                 * -1 -> ReadQueue doubles its buffer (128->256->512...) and re-reads until it fits. */
                LOG("Q_read <- queue 0x%x size %u (TOO BIG for buf %u: ret -12 errno=ENOMEM; not dequeued; caller doubles+re-reads)\n",id,full,maxsize);
                errno=ENOMEM;                                     /* set LAST: LOG()/fprintf may clobber errno */
                return -12;                                       /* 0xfffffff4 + errno ENOMEM: real-kernel "buffer too small" */
            }
            uint32_t copied=full;
            if(s>=0&&s<1024) wmq_empty_streak[s]=0;   /* WMQ_BREAK: a real read resets the empty streak */
            if(buf&&copied) memcpy(buf,q->msg[slot].data,copied);
            if(hdrbuf){ hdrbuf[0]=q->msg[slot].hdr[0]; hdrbuf[1]=q->msg[slot].hdr[1]; hdrbuf[2]=q->msg[slot].hdr[2]; }
            uint32_t len=copied;
            qhex("Q_read",id,buf,len);
            uint32_t qowner=q->owner, qnotify=q->notify_bits;
            if(pos==q->head){
                __atomic_add_fetch(&q->head,1,__ATOMIC_ACQ_REL);  /* normal FIFO dequeue */
            } else {
                /* out-of-order family match: remove slot `pos`, shift [pos+1..tail-1] down by one */
                for(uint32_t i=pos; i+1!=q->tail; i++) q->msg[i%QSLOTS]=q->msg[(i+1)%QSLOTS];
                __atomic_sub_fetch(&q->tail,1,__ATOMIC_ACQ_REL);
                LOG("Q_read RTS_FAMILY_ROUTE: out-of-order dequeue at pos %u (queue 0x%x \"%s\", reader t%x)\n",
                    pos,id,C->queues[s].name,task_self());
            }
            int more = (q->tail != q->head);            /* queue still non-empty after this read? */
            unlock();
            /* LEVEL-TRIGGERED queue notify: the kernel event word is a BITMASK, so N sends to a queue set
             * its notify bit ONCE; after the owner reads one message and clears the bit, the remaining
             * messages would be stranded (HrMmi read 1 of 7 flushed config msgs then waited forever). The
             * real kernel keeps the queue readable while non-empty -> re-assert the notify bit so the
             * EVHandler dispatcher wakes again and drains the queue. */
            if(more && qnotify && qowner){ ev_send(qowner, qnotify);
                LOG("Q_read re-assert notify %08x->task 0x%x (queue 0x%x still has %u msgs, reader=t%x)\n",
                    qnotify,qowner,id,q->tail-q->head,task_self()); }
            LOG("Q_read <- queue 0x%x size %u\n",id,len);
            { uint32_t mtag = (buf && len>=4) ? *(const uint32_t*)buf : 0;
              HST(qowner,0,"QR [%x]\"%s\" size=%u tag=%08x (rdr=t%x, remain=%u) [%s]\n",id,C->queues[s].name,len,mtag,task_self(),q->tail-q->head,msascii(buf,len));
              /* EVTERR_DEFER release: HrMmi just read the HrMmiCfgGlobal (type 0x290081 = the active-state
               * target bootstrap, OnHrMmiCfgGlobal). Now post the deferred EvtAns so its OneRequestDone fires
               * MoveActiveStateTowardsTarget AFTER the target is set -> Activate -> UpdateDisplay -> window. */
              if(deferred_evterr_rqid && (mtag&0x7fffffff)==0x290081){
                  uint32_t rq=deferred_evterr_rqid; deferred_evterr_rqid=0;
                  LOG("EVTERR_DEFER: HrMmiCfgGlobal(0x290081) read -> releasing deferred EvtAnsErrorRequest to 0x%x\n",rq);
                  post_evt_ans_error(rq); } }
            /* REPLAY_TRIGGER: the first post-runup 69-byte read from CfgServerQueue is IPO's
             * connect; it's now dequeued+about-to-be-registered-pending. Re-inject the recorded
             * startup self-messages so a SIK handler re-runs SendConnected and flushes IPO's ACK.
             * FIFO order guarantees the connect is processed before these are read. */
            if(replay_trigger && runup_done && !trigger_replayed
               && len==69 && !strcmp(q->name,"CfgServerQueue")){
                trigger_replayed=1;
                LOG("REPLAY: connect read -> re-injecting %d startup msgs to flush ACK\n",cfgq_n);
                int n=cfgq_n; if(n>CFGQ_CAP)n=CFGQ_CAP;
                for(int i=0;i<n;i++) q_send(id,cfgq_rec[i].data,cfgq_rec[i].len,0);
            }
            /* INJECT_UPD: impersonate the MMI — inject a synthetic UpdNewState (id 0x1f0320) onto
             * CfgServerQueue after IPO's connect is read (FIFO: connect processed → registered pending
             * → UpdNewState read → OnUpdNewState → SendConnected flushes IPO's ACK). v1 = header + a
             * GMsgString layer field; iterate the format until OnUpdNewState reaches SendConnected. */
            if(inject_upd && runup_done && !trigger_replayed
               && len==69 && !strcmp(q->name,"CfgServerQueue")){
                trigger_replayed=1;
                /* v2: header(0x001f0320) + 1 real GMsgString(layer "Nc") + markers so the field COUNT
                 * matches UpdNewState's schema (~12 fields; .rodata@0x232040 types e7,63,e7,nested,c6×5)
                 * — avoids the deserializer reading PAST the buffer (the v1 crash). Marker dwords
                 * 0x800000XX use UpdNewState's schema field-type low-bytes (63,e7,c6…). */
                static const uint8_t upd[] = {
                    0x20,0x03,0x1f,0x00,                                   /* header 0x001f0320 */
                    0xe7,0x00,0x00,0x00, 0x02,0x00,0x00,0x00, 0x4e,0x63,   /* field0 GMsgString "Nc" (layer) */
                    0x63,0x00,0x00,0x80,                                   /* field1 marker type 0x63 */
                    0xe7,0x00,0x00,0x80,                                   /* field2 marker GMsgString (empty) */
                    0x0b,0x03,0x1f,0x80,                                   /* field3 marker nested 0x1f030b */
                    0xc6,0x00,0x00,0x80,                                   /* field4 marker 0xc6 */
                    0xc6,0x00,0x00,0x80,                                   /* field5 */
                    0xc6,0x00,0x00,0x80,                                   /* field6 */
                    0xc6,0x00,0x00,0x80,                                   /* field7 */
                    0xc6,0x00,0x00,0x80,                                   /* field8 */
                    0xe7,0x00,0x00,0x80,                                   /* field9 pad marker */
                    0xc6,0x00,0x00,0x80,                                   /* field10 pad marker */
                    0xe7,0x00,0x00,0x80                                    /* field11 pad marker */
                };
                LOG("INJECT_UPD: connect read -> injecting synthetic UpdNewState (%u bytes, id 0x1f0320)\n",(unsigned)sizeof upd);
                q_send(id,upd,(uint32_t)sizeof upd,0);
            }
            return (int)len;                              /* message size in eax */
            }   /* if(have) */
        }
        uint32_t t=q->tail; unlock();
        /* empty/timeout => "no message". errno MUST NOT be ENOMEM here, or the libheros q_read
         * wrapper would misread this negative return as "too big" and the caller would grow+spin
         * on an empty queue. EAGAIN (!=12, !=4) makes the wrapper return 0 = clean "no message". */
        if(timeout==0){
            /* DIAG: reveal the non-blocking poll set (which empty queues a busy-spinning client polls).
             * HEROSCALL_EMPTYPOLL_DIAG=1 -> log a per-queue running count every 100000 empties. */
            static int diag=-1; if(diag<0){ const char*e=getenv("HEROSCALL_EMPTYPOLL_DIAG"); diag=e&&e[0]=='1'; }
            if(diag){ static unsigned long ec[1024]; static unsigned long tot;
                if(s>=0&&s<1024) ec[s]++; if((++tot%2000UL)==0){
                    LOG("EMPTYPOLL_DIAG: %lu empty no-wait reads; per-queue:",tot);
                    for(int i=0;i<1024;i++) if(ec[i]) fprintf(stderr," 0x%x(\"%s\")=%lu",C->queues[i].id,C->queues[i].name,ec[i]);
                    fprintf(stderr,"\n"); }
            }
            /* EMPTYPOLL_YIELD: a WM client (skmgr) whose window-manager peer is absent busy-spins a
             * non-blocking Q_read on its (empty) WM event queue, pegging a core and STARVING the
             * co-resident Guppy FEX process so Guppy never reaches its SkMgrLogin send. Throttle the
             * empty no-wait read with a short usleep so the OS scheduler gives Guppy CPU. Faithful: a
             * genuinely-empty non-blocking poll returns "no message" either way; only the spin rate
             * changes (the real system never spins this tight because winmgr feeds events). */
            static int yld=-1; if(yld<0){ const char*e=getenv("HEROSCALL_EMPTYPOLL_YIELD"); yld=e?atoi(e):0; }
            if(yld>0) usleep((useconds_t)yld);
            /* WMQ_BREAK: a PLib WM-event drain (WmRecvReplyEx->WmRead@libwinmgrlib 0x3cc0) busy-polls a
             * "WMQ<task>" reply queue with NOWAIT q_read, looping WHILE errno==EAGAIN(11). With no live
             * winmgr the next WM event never comes, so it spins forever and STARVES the FThread dispatch
             * from servicing Q_SkMgr (the softkey-login serve queue). WmRead TERMINATES its loop (returns
             * 0 = "no message") when errno is NEITHER EAGAIN(11) NOR EINTR(4) — e.g. ETIMEDOUT(110). After
             * N consecutive empty no-wait reads on a WMQ* queue, return ETIMEDOUT so WmRead returns 0, the
             * WM-drain loop ends, and the dispatch regains control to check Q_SkMgr (and block/cycle on its
             * full waitable mask incl. the Q_SkMgr notify 0x02000000). Faithful: the real WM drain returns
             * "no more events" on an empty queue; the emulator's perpetual-EAGAIN turned that into a
             * livelock. Counter resets on a real read (above). */
            { static int wmq_break=-1; static long wmq_thresh=0;
              if(wmq_break<0){ const char*e=getenv("HEROSCALL_WMQ_BREAK"); wmq_break=e&&e[0]=='1';
                  const char*t=getenv("HEROSCALL_WMQ_BREAK_N"); wmq_thresh=t?atol(t):2000; }
              if(wmq_break && s>=0 && s<1024 && !strncmp(C->queues[s].name,"WMQ",3)){
                  /* RESPONSIVENESS: the WM event-drain found no WM event. If the dispatch task already
                   * has a PENDING queue-notify for a NON-WM queue (e.g. Q_SkMgr's 0x02000000 softkey
                   * login from Guppy), break the WM pump IMMEDIATELY so the FThread dispatch returns to
                   * Ev_receive(0x07011000) and services that queue — instead of busy-draining the empty
                   * WM queue N more times (which starves the co-registered Q_SkMgr serve queue). The WM
                   * queue's OWN notify bit is excluded so a genuine pending WM event still drains here.
                   * Faithful: the real WM drain returns "no more events" and yields to the next waitable;
                   * the emulator's perpetual-EAGAIN turned that yield into a livelock. */
                  int ts=task_slot(task_self());
                  if(ts>=0){ uint32_t pend = C->tasks[ts].events & 0xff000000u & ~C->queues[s].notify_bits;
                      if(pend){ wmq_empty_streak[s]=0; errno=ETIMEDOUT; return -0x6e; } }
                  if(++wmq_empty_streak[s] >= (unsigned long)wmq_thresh){
                      wmq_empty_streak[s]=0;
                      errno=ETIMEDOUT; return -0x6e;   /* 110: WmRead -> return 0 = drain done; dispatch regains control */
                  }
              }
            }
            errno=EAGAIN; return -0x35;
        }     /* empty, no-wait */
        struct timespec ts,*tp=0;
        if(timeout!=0xffffffff){ ts.tv_sec=timeout/1000; ts.tv_nsec=(timeout%1000)*1000000L; tp=&ts; }
        futex(&q->tail,FUTEX_WAIT,t,tp);
        if(timeout!=0xffffffff && q->tail==q->head){ errno=EAGAIN; return -0x35; }   /* timed out */
    }
}

/* ---------------- experimental HWS (hardware-server) stub ----------------
 * The HWS run-up (HwsMailslotQueue::Create) sends an 83-byte "GetData" request to
 * the queue "QHWServer" (the host-side IOsim, which has NO i386 binary) and blocks
 * on SyncMessage reading its temp reply mailslot "HwsM<task>N<ctr>". The run-up
 * proceeds only when the reply makes HWSSrvConnected.status(+0x20)==0 (disasm of
 * Create @0x21384). With HEROSCALL_HWS_STUB=1 we synthesise a reply: extract the
 * "HwsM…" reply-to name embedded in the request and post a reply to it.
 * v1 = echo the request back (a valid GMessage) — an empirical probe of whether the
 * deserializer/status path accepts any well-formed reply, or needs specific fields. */
static void hws_autoreply(uint32_t target_qid,const void*msg,uint32_t size){
    if(!hws_stub||!msg||size<20) return;
    int s=q_slot(target_qid); if(s<0||strcmp(C->queues[s].name,"QHWServer")) return;
    const unsigned char*m=msg; int at=-1;
    for(uint32_t i=0;i+16<=size;i++) if(m[i]=='H'&&m[i+1]=='w'&&m[i+2]=='s'&&m[i+3]=='M'){ at=(int)i; break; }
    if(at<0){ LOG("HWS stub: no HwsM reply-to in request (%u bytes)\n",size); return; }
    char rname[NAMELEN]; int n=0; for(;n<16&&at+n<(int)size;n++) rname[n]=(char)m[at+n]; rname[n]=0;
    int rs=q_find_slot(rname); if(rs<0){ LOG("HWS stub: reply queue \"%s\" not found\n",rname); return; }
    uint32_t rqid=C->queues[rs].id;
    q_send(rqid,msg,size,0);                              /* v1: echo the request as the reply */
    LOG("HWS stub: replied %u bytes to \"%s\" (0x%x) [echo]\n",size,rname,rqid);
    __atomic_store_n(&runup_done,1,__ATOMIC_RELEASE);    /* run-up complete: stop recording, arm replay */
    /* UNCONDITIONAL readiness marker (not VERBOSE-gated): a coordinator (run_hrmmi) waits for this
     * before launching client processes, so ConfigServer's run-up + the INJECT_REREAD config-data
     * load complete BEFORE a client's messages arrive — else the load races the client's writes to
     * CfgServerQueue and corrupts the heap (free(): invalid pointer in ReadConfigDataSet). */
    { fprintf(stderr,"[rtos] RUNUP_COMPLETE pid=%d\n",(int)getpid()); fflush(stderr); }
    /* INJECT_REREAD: now that ConfigServer's run-up is done, post a synthetic UpdNewState onto
     * CfgServerQueue. OnUpdNewState → CfgServer::ReadConfigDataSet → ReadDataFiles LOADS the config
     * DATA files (tnc.cfg → "NC" channel group). (CfgRereadData/OnRereadData is write-back/refresh,
     * NOT the initial load — ReadDataFiles' callers are ReadConfigDataSet, reached via OnUpdNewState.)
     * Posting at run-up loads the config BEFORE IPO connects+queries. Needs /etc/jhvolume colon-form
     * so the "SYS:\config\..." paths resolve. UpdNewState wire = the proven v2 schema (id 0x1f0320). */
    post_inject_reread();
}
/* Post the synthetic UpdNewState (id 0x1f0320, "Nc" layer) onto CfgServerQueue so ConfigServer's
 * OnUpdNewState → ReadConfigDataSet → ReadDataFiles loads the FULL config DATA (tnc.cfg → the "NC"
 * channel group, channel.cfg, …) — the cascade that the run-up alone leaves incomplete (indices +
 * .atr only). Fired from BOTH run-up paths (the HWS stub AND the serve-loop fallback). Idempotent. */
static void post_inject_reread(void){
    if(!inject_reread || reread_injected) return;
    int cs=q_find_slot("CfgServerQueue");
    if(cs<0) return;
    reread_injected=1;
    static const uint8_t upd[]={
        0x20,0x03,0x1f,0x00,                                   /* header 0x1f0320 = UpdNewState */
        0xe7,0x00,0x00,0x00, 0x02,0x00,0x00,0x00, 0x4e,0x63,   /* field0 GMsgString "Nc" (layer) */
        0x63,0x00,0x00,0x80, 0xe7,0x00,0x00,0x80, 0x0b,0x03,0x1f,0x80,
        0xc6,0x00,0x00,0x80, 0xc6,0x00,0x00,0x80, 0xc6,0x00,0x00,0x80,
        0xc6,0x00,0x00,0x80, 0xc6,0x00,0x00,0x80, 0xe7,0x00,0x00,0x80,
        0xc6,0x00,0x00,0x80, 0xe7,0x00,0x00,0x80                /* markers to match the ~12-field schema */
    };
    uint32_t qid=C->queues[cs].id;
    q_send(qid,upd,(uint32_t)sizeof upd,0);
    LOG("INJECT_REREAD: posted UpdNewState to CfgServerQueue (0x%x) -> ReadConfigDataSet loads config\n",qid);
}
/* INJECT_ACK: IPO sent CfgConnectClient(0x1700c0). Parse its reply-queue name (first GMsgString)
 * and post a synthetic CfgClientIsConnected(0x170100, success=OK) to it so IPO proceeds. */
static void put32(unsigned char*b,uint32_t v){ b[0]=v; b[1]=v>>8; b[2]=v>>16; b[3]=v>>24; }

/* ---------------- GMessage serializer for INJECT_FMLOAD ---------------- */
/* wire type codes (from GMessageData +0x10, libgmsglib/libGMessageShared) */
#define GMC_STRING 0xe7
#define GMC_INT    0x63
#define GMC_LIST   0x18c
#define GMC_ENUM_SUBSYS_TYPE 0xc8014b   /* real wire type-id from FM_SUBSYSTEM_TYPE ctor push 0xc8014b (NOT the GMessageData 0x12 "kind") */
/* append a PRESENT GMsgString attribute (tag 0xe7, u32 len, bytes-no-null) */
static uint32_t gm_str(unsigned char*b,uint32_t p,const char*s){
    put32(b+p,GMC_STRING); p+=4; uint32_t n=(uint32_t)strlen(s);
    put32(b+p,n); p+=4; memcpy(b+p,s,n); p+=n; return p;
}
/* append an ABSENT attribute marker (tag 0x80000000|code) */
static uint32_t gm_absent(unsigned char*b,uint32_t p,uint32_t code){
    put32(b+p,0x80000000u|code); p+=4; return p;
}
/* append a PRESENT GMsgInt attribute (tag 0x63, u32 value) */
static uint32_t gm_int(unsigned char*b,uint32_t p,uint32_t v){
    put32(b+p,GMC_INT); p+=4; put32(b+p,v); p+=4; return p;
}
/* append a PRESENT-EMPTY GMsgList (tag 0x18c, count=0). NOTE: a list does NOT use the absent top-bit —
 * sending 0x8000018c corrupts the heap; the correct empty form is {type-id, count=0} (subagent RE). */
static uint32_t gm_list_empty(unsigned char*b,uint32_t p,uint32_t code){
    put32(b+p,code); p+=4; put32(b+p,0); p+=4; return p;
}
/* append a PRESENT enum/scalar attribute (tag=full type-id, u32 value) */
static uint32_t gm_scalar(unsigned char*b,uint32_t p,uint32_t code,uint32_t v){
    put32(b+p,code); p+=4; put32(b+p,v); p+=4; return p;
}
/* Build FmLoadProcess (type 0xc80161). 7 attrs in schema order. Pass NULL for absent string attrs.
 * HEROSCALL_INJECT_FMLOAD_PRESENT=1 makes the enum(attr5)+int(attr6) PRESENT with value 0 (the
 * "normal" process create-mode) instead of absent — the bare inject bypasses Config/Subsystems which
 * normally set these; absent may leave PCreatePrepare's create-mode non-default (skips PCreate->fork). */
static int fmload_present=-1;
static uint32_t gm_build_fmloadprocess(unsigned char*b,const char*procName,const char*cmdOpts,const char*imagePath){
    if(fmload_present<0){ const char*e=getenv("HEROSCALL_INJECT_FMLOAD_PRESENT"); fmload_present=e&&e[0]=='1'; }
    uint32_t p=0; put32(b+p,0x00c80161u); p+=4;            /* type-id */
    p = procName ? gm_str(b,p,procName) : gm_absent(b,p,GMC_STRING);   /* 0 processName */
    p = cmdOpts  ? gm_str(b,p,cmdOpts)  : gm_absent(b,p,GMC_STRING);   /* 1 commandLineOptions */
    p = gm_list_empty(b,p,GMC_LIST);                                   /* 2 ifDefined (present-empty list) */
    p = gm_absent(b,p,GMC_STRING);                                     /* 3 forEach (absent) */
    p = imagePath? gm_str(b,p,imagePath): gm_absent(b,p,GMC_STRING);   /* 4 imagePath */
    if(fmload_present){
        p = gm_scalar(b,p,GMC_ENUM_SUBSYS_TYPE,0);                     /* 5 FM_SUBSYSTEM_TYPE present=0 */
        p = gm_int(b,p,0);                                             /* 6 GMsgInt present=0 */
    }else{
        p = gm_absent(b,p,GMC_ENUM_SUBSYS_TYPE);                       /* 5 (absent) */
        p = gm_absent(b,p,GMC_INT);                                    /* 6 (absent) */
    }
    return p;
}
#define GMC_ENUM_SUBSYS_ACTION 0xca004b   /* FM_SUBSYSTEM_ACTION enum (ctor push 0xca004b) */
/* Build FmSubsystemAction (type 0xca0060). 3 attrs: action(enum), name(str), procedure(str).
 * action=0 = REGISTER (Subsystems::OnMessage(FmSubsystemAction) case 0 → push_back). */
static uint32_t gm_build_fmsubsystemaction(unsigned char*b,uint32_t action,const char*name,const char*procedure){
    uint32_t p=0; put32(b+p,0x00ca0060u); p+=4;                        /* type-id */
    p = gm_scalar(b,p,GMC_ENUM_SUBSYS_ACTION,action);                 /* 0 action (present enum) */
    p = name ? gm_str(b,p,name) : gm_absent(b,p,GMC_STRING);          /* 1 name */
    p = procedure&&procedure[0] ? gm_str(b,p,procedure) : gm_absent(b,p,GMC_STRING); /* 2 procedure */
    return p;
}
/* Build FmLoadSubsystem (type 0xc80181). 6 attrs: name(str), localNamespace(str), procedure(str),
 * type(enum), processes(GMsgList), int. The processes list = PRESENT {0x18c, count=1, <FmLoadProcess>}.
 * Flows through Config (ReplaceToken ~->localNS, ConvertWithEnvVar) + Subsystems (explode + set the
 * +128/+132 runtime fields PCreatePrepare reads for the create-mode). Subsystem must be REGISTERED first. */
static uint32_t gm_build_fmloadsubsystem(unsigned char*b,const char*name,const char*localNS,
                                         const char*procName,const char*cmdOpts,const char*imagePath){
    uint32_t p=0; put32(b+p,0x00c80181u); p+=4;                        /* type-id */
    p = gm_str(b,p,name);                                             /* 0 subsystem name */
    p = gm_str(b,p,localNS);                                          /* 1 localNamespace */
    p = gm_absent(b,p,GMC_STRING);                                    /* 2 procedure (absent) */
    p = gm_absent(b,p,GMC_ENUM_SUBSYS_TYPE);                          /* 3 type FM_SUBSYSTEM_TYPE (absent=normal) */
    /* 4 processes: PRESENT GMsgList {0x18c, count=1, FmLoadProcess sub-message} */
    put32(b+p,GMC_LIST); p+=4; put32(b+p,1); p+=4;                    /* list header + count=1 */
    p += gm_build_fmloadprocess(b+p,procName,cmdOpts,imagePath);      /* the one FmLoadProcess element */
    p = gm_absent(b,p,GMC_INT);                                       /* 5 int (absent) */
    return p;
}
/* The constellation set to inject. For the first milestone we inject ONE flat FmLoadProcess (winmgr)
 * — Processes::OnMessage(FmLoadProcess) forks it directly (no subsystem registration required).
 * imagePath must be the RESOLVED path (Config's %EXECDIRH% expansion is bypassed): EXECDIRH=/tmp/b. */
struct fmproc { const char*procName; const char*cmdOpts; const char*imagePath; };
static const struct fmproc fmload_tbl[] = {
    { "winmgr/winmgr", 0, "/tmp/b/winmgr.elf" },
};
static void dump_bytes(const char*tag,const unsigned char*b,uint32_t n){
    fprintf(stderr,"[rtos] %s (%u bytes)=",tag,n); for(uint32_t k=0;k<n&&k<400;k++) fprintf(stderr,"%02x",b[k]); fprintf(stderr,"\n"); fflush(stderr);
}
static void inject_fmload_set(uint32_t qid){
    unsigned char buf[2048];
    const char*one=getenv("HEROSCALL_INJECT_FMLOAD_IMG");   /* optional override of the single image path */
    const char*onep=getenv("HEROSCALL_INJECT_FMLOAD_PROC");
    const char*sub=getenv("HEROSCALL_INJECT_SUBSYS");        /* 1 = faithful FmSubsystemAction(register)+FmLoadSubsystem path */
    if(sub&&sub[0]=='1'){
        /* FAITHFUL PATH: register the subsystem, then load it (Config expands+sets fields, Subsystems
         * explodes to FmLoadProcess with the +128/+132 create-mode fields → PCreate forks). */
        const char*nm="winmgr", *ns="winmgr";
        const char*pn = onep?onep:"~/winmgr";               /* RAW: Config ReplaceToken ~->localNS */
        const char*ip = one ?one :"/tmp/b/winmgr.elf";      /* pre-resolved (Config ConvertWithEnvVar leaves it) */
        uint32_t n;
        /* By default DO NOT pre-register: Procedures::OnMessageFromProcedure(FmLoadSubsystem)@0x4bdf0
         * registers the subsystem itself (creates FmSubsystemAction) then forwards+explodes. A separate
         * pre-register makes Procedures see it "already loaded" -> "...twice" error -> drops the load
         * (no explode). HEROSCALL_INJECT_SUBSYS_REG=1 re-enables the explicit register. */
        const char*reg=getenv("HEROSCALL_INJECT_SUBSYS_REG");
        if(reg&&reg[0]=='1'){
            n=gm_build_fmsubsystemaction(buf,0,nm,0);        /* action=0 REGISTER */
            fprintf(stderr,"[rtos] INJECT_SUBSYS: FmSubsystemAction(register \"%s\") -> AppStartMaster(0x%x)\n",nm,qid);
            dump_bytes("INJECT_SUBSYS reg",buf,n); q_send(qid,buf,n,0);
        }
        n=gm_build_fmloadsubsystem(buf,nm,ns,pn,0,ip);       /* load: name,localNS,proc,cmdOpts=absent,image */
        fprintf(stderr,"[rtos] INJECT_SUBSYS: FmLoadSubsystem(\"%s\" proc=\"%s\" img=\"%s\") -> AppStartMaster(0x%x)\n",nm,pn,ip,qid);
        dump_bytes("INJECT_SUBSYS load",buf,n); q_send(qid,buf,n,0);
        return;
    }
    /* FULL SET: HEROSCALL_INJECT_FMLOAD_SET=<file> with "procName|imagePath" per line (the whole
     * constellation, generated from batch/TNC640heros.txt). Inject an FmLoadProcess for EACH -> each
     * forks+launches via the p_create FEX-spawn interposer. HEROSCALL_INJECT_FMLOAD_MAX caps the count. */
    const char*setf=getenv("HEROSCALL_INJECT_FMLOAD_SET");
    if(setf&&setf[0]){
        FILE*f=fopen(setf,"r");
        if(!f){ fprintf(stderr,"[rtos] INJECT_FMLOAD[set]: cannot open %s\n",setf); }
        else {
            const char*mx=getenv("HEROSCALL_INJECT_FMLOAD_MAX"); int maxn=mx&&mx[0]?atoi(mx):100000;
            char line[1024]; int cnt=0;
            while(fgets(line,sizeof line,f)){
                char*nl=strchr(line,'\n'); if(nl)*nl=0;
                if(!line[0]||line[0]=='#') continue;
                char*bar=strchr(line,'|'); if(!bar) continue; *bar=0;
                const char*pn=line; const char*ip=bar+1;
                if(cnt>=maxn) break;
                uint32_t n=gm_build_fmloadprocess(buf,pn,0,ip);
                fprintf(stderr,"[rtos] INJECT_FMLOAD[set %d]: FmLoadProcess(proc=\"%s\" img=\"%s\") %u bytes -> 0x%x\n",cnt,pn,ip,n,qid);
                q_send(qid,buf,n,0); cnt++;
            }
            fclose(f);
            fprintf(stderr,"[rtos] INJECT_FMLOAD[set]: injected %d FmLoadProcess from %s\n",cnt,setf);
            return;
        }
    }
    for(unsigned i=0;i<sizeof fmload_tbl/sizeof fmload_tbl[0];i++){
        const char*pn = (i==0&&onep)?onep:fmload_tbl[i].procName;
        const char*ip = (i==0&&one )?one :fmload_tbl[i].imagePath;
        uint32_t n=gm_build_fmloadprocess(buf,pn,fmload_tbl[i].cmdOpts,ip);
        fprintf(stderr,"[rtos] INJECT_FMLOAD: posting FmLoadProcess(proc=\"%s\" img=\"%s\") %u bytes -> AppStartMaster(0x%x)\n",pn,ip,n,qid);
        dump_bytes("INJECT_FMLOAD bytes",buf,n);
        q_send(qid,buf,n,0);
    }
}

static uint32_t acked_qids[32]; static int n_acked=0;   /* per-reply-queue dedup (HrMmi has several) */
static void inject_connect_ack(uint32_t target_qid,const void*msg,uint32_t size){
    if(!inject_ack||!msg||size<16) return;
    int ts=q_slot(target_qid); if(ts<0||strcmp(C->queues[ts].name,"CfgServerQueue")) return;
    const unsigned char*m=msg;
    uint32_t hdr=m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24);
    if((hdr&0x7fffffff)!=0x1700c0) return;               /* not a CfgConnectClient */
    uint32_t tag=m[4]|(m[5]<<8)|(m[6]<<16)|((uint32_t)m[7]<<24);
    if(tag!=0xe7) return;                                 /* expect GMsgString reply-queue name */
    uint32_t nlen=m[8]|(m[9]<<8)|(m[10]<<16)|((uint32_t)m[11]<<24);
    if(nlen==0||nlen>64||12+nlen>size) return;
    char name[80]; memcpy(name,m+12,nlen); name[nlen]=0;
    const char *cid=name; uint32_t cidlen=nlen;
    int rs=q_find_slot(name);
    if(rs<0){
        /* HrMmi's CfgConnectClient field0 is NOT a clean reply-queue name like IPO's
         * ("0-0000106CfgM") — it's a descriptive string that EMBEDS the queue name, e.g.
         * " New start of HrMmi logging with m.QueueHrMmi" (real queue = "QueueHrMmi").
         * Find the existing queue whose name appears as a substring of field0 and use ITS
         * name as the reply target + ACK clientId. CRITICAL: a CfgClientIsConnected ACK must
         * land on a queue the client is actually WAITING on, i.e. one with notify_bits != 0
         * (a real input/reply queue) — NOT a black-hole/logger queue. field0 itself embeds a
         * logger-banner queue (" New start of HrMmi logging wit", notify 0) that is LONGER
         * than the real reply queue "QueueHrMmi" (notify 0x02000000), so a plain longest-wins
         * match picks the black hole and the ACK never wakes HrMmi's Ev_receive(0x03011001).
         * => PREFER notify!=0 (longest among them); only fall back to a notify=0 match if none. */
        int best=-1; size_t bestlen=0; int best_n=-1; size_t bestlen_n=0;
        for(int qi=0;qi<MAXQ;qi++){ struct queue*q=&C->queues[qi];
            if(q->used && q->name[0] && strlen(q->name)>=4 && strstr(name,q->name)){
                size_t l=strlen(q->name);
                if(l>bestlen){ bestlen=l; best=qi; }                       /* longest, any notify */
                if(q->notify_bits && l>bestlen_n){ bestlen_n=l; best_n=qi; } } }  /* longest WITH notify */
        if(best_n>=0){ best=best_n; bestlen=bestlen_n; }                   /* prefer a watched queue */
        if(best>=0){ rs=best; cid=C->queues[best].name; cidlen=(uint32_t)bestlen;
            LOG("INJECT_ACK: matched embedded reply queue \"%s\" (notify %08x) in field0 \"%s\"\n",
                cid,C->queues[best].notify_bits,name); }
    }
    if(rs<0){ LOG("INJECT_ACK: reply queue \"%s\" not found\n",name); return; }
    uint32_t rqid=C->queues[rs].id;
    /* CfgClientIsConnected: header + clientId(name) + id(marker) + success(enum=OK 0) = 3 fields */
    unsigned char ack[160]; uint32_t p=0;
    put32(ack+p,0x00170100); p+=4;                        /* header (CfgClientIsConnected id) */
    put32(ack+p,0x000000e7); p+=4;                        /* field0 clientId: GMsgString tag (present) */
    put32(ack+p,cidlen); p+=4; memcpy(ack+p,cid,cidlen); p+=cidlen;
    put32(ack+p,0x80000063); p+=4;                        /* field1 id: marker (absent/default) */
    put32(ack+p,0x001700eb); p+=4;                        /* field2 success: enum tag (present) */
    put32(ack+p,0x00000000); p+=4;                        /* success value = OK(0) */
    for(int i=0;i<n_acked;i++) if(acked_qids[i]==rqid) return;  /* already ACKed this reply queue */
    if(n_acked<32) acked_qids[n_acked++]=rqid;
    q_send(rqid,ack,p,0);
    LOG("INJECT_ACK: posted CfgClientIsConnected(success=OK) to \"%s\" (0x%x), %u bytes\n",cid,rqid,p);
}

/* INJECT_EVT_ACK: HrMmi's EvtConnectClient(0x3200c0)->QEvtServer mirrors CfgConnectClient->CfgServerQueue.
 * Same reply-to resolution (the leading GMsgString embeds the watched queue, e.g. ".QueueHrMmi"); reply
 * with EvtClientIsConnected(0x3200a0) so OnEvtConnected proceeds. See the HEROSCALL_INJECT_EVT_ACK note. */
static uint32_t evt_acked_qids[32]; static int n_evt_acked=0;
static void inject_evt_connect_ack(uint32_t target_qid,const void*msg,uint32_t size){
    if(!inject_evt_ack||!msg||size<16) return;
    int ts=q_slot(target_qid); if(ts<0||strcmp(C->queues[ts].name,"QEvtServer")) return;
    const unsigned char*m=msg;
    uint32_t hdr=m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24);
    /* EvtConnectClient wire type-id = 0x320081 (RE'd empirically: HrMmi's 67B connect send carries hdr
     * 0x320081 + leading GMsgString reply-to; its 520B 0x320221 send is an EvtSendEvent publish, NOT a
     * connect). The 0x3200c0 schema in libGMessageMisc .rodata is a different message. */
    if((hdr&0x7fffffff)!=0x320081) return;               /* not an EvtConnectClient (ignore publishes) */
    uint32_t tag=m[4]|(m[5]<<8)|(m[6]<<16)|((uint32_t)m[7]<<24);
    if(tag!=0xe7) return;                                 /* expect GMsgString reply-to (idclient) */
    uint32_t nlen=m[8]|(m[9]<<8)|(m[10]<<16)|((uint32_t)m[11]<<24);
    if(nlen==0||nlen>64||12+nlen>size) return;
    char name[80]; memcpy(name,m+12,nlen); name[nlen]=0;
    int rs=q_find_slot(name);
    if(rs<0){
        /* identical to INJECT_ACK: field0 EMBEDS the queue name; prefer the longest WATCHED
         * (notify!=0) substring queue (QueueHrMmi 0x02000000), else longest any. */
        int best=-1; size_t bestlen=0; int best_n=-1; size_t bestlen_n=0;
        for(int qi=0;qi<MAXQ;qi++){ struct queue*q=&C->queues[qi];
            if(q->used && q->name[0] && strlen(q->name)>=4 && strstr(name,q->name)){
                size_t l=strlen(q->name);
                if(l>bestlen){ bestlen=l; best=qi; }
                if(q->notify_bits && l>bestlen_n){ bestlen_n=l; best_n=qi; } } }
        if(best_n>=0){ best=best_n; }
        if(best>=0){ rs=best;
            LOG("INJECT_EVT_ACK: matched embedded reply queue \"%s\" (notify %08x) in field0 \"%s\"\n",
                C->queues[best].name,C->queues[best].notify_bits,name); }
    }
    if(rs<0){ LOG("INJECT_EVT_ACK: reply queue \"%s\" not found\n",name); return; }
    uint32_t rqid=C->queues[rs].id;
    for(int i=0;i<n_evt_acked;i++) if(evt_acked_qids[i]==rqid) return;   /* one ACK per reply queue */
    if(n_evt_acked<32) evt_acked_qids[n_evt_acked++]=rqid;
    /* EvtClientIsConnected: header + Success(int 0=OK) + stateError(int 0) + viewerHandle(int 0) */
    unsigned char ack[64]; uint32_t p=0;
    put32(ack+p,0x003200a0); p+=4;                        /* header (EvtClientIsConnected id) */
    put32(ack+p,0x00000063); p+=4; put32(ack+p,0); p+=4;  /* Success:      GMsgInt present = 0 (OK) */
    put32(ack+p,0x00000063); p+=4; put32(ack+p,0); p+=4;  /* stateError:   GMsgInt present = 0 */
    put32(ack+p,0x00000063); p+=4; put32(ack+p,0); p+=4;  /* viewerHandle: GMsgInt present = 0 */
    q_send(rqid,ack,p,0);
    LOG("INJECT_EVT_ACK: posted EvtClientIsConnected(success) to \"%s\" (0x%x), %u bytes\n",C->queues[rs].name,rqid,p);
}

/* INJECT_PEER_ACK: synthesize the operational-peer connect replies (IPO/PLC/CM) and post them to the
 * client's reply queue, draining HrMmi's request counter -> MoveActiveStateTowardsTarget. See the
 * HEROSCALL_INJECT_PEER_ACK note above. Reply = [reply-type-id][per-schema-field ABSENT tag]. */
static uint32_t peer_acked[64]; static int n_peer_acked=0;   /* dedup per (reply-queue<<8 | peer) */
static int cm_grant=-1;                                       /* HEROSCALL_CM_GRANT (default ON) */
static void inject_peer_connect_ack(uint32_t target_qid,const void*msg,uint32_t size){
    if(!inject_peer_ack||!msg||size<16) return;
    if(cm_grant<0){ const char*e=getenv("HEROSCALL_CM_GRANT"); cm_grant=(!e||e[0]!='0'); } /* default ON */
    const unsigned char*m=msg;
    uint32_t hdr=(m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24))&0x7fffffff;
    /* request type-id -> (peer, reply type-id, field codes[], present?[], values[]). Codes carry NO top
     * bit; ABSENT field = 0x80000000|code (leaves it 0), PRESENT field = [code][value]. Schemas RE'd from
     * the lib schema tables + each reply handler's body reads (HrModule::On*@HrMmi.elf, idalib). */
    struct { uint32_t req, reply; uint8_t peer; uint32_t code[8]; uint8_t pres[8]; uint32_t val[8]; int nf; } P[]={
      /* IpoSrvLoginQuit: OnIpoSrvConnected@0x35ca0 needs body+4(field0)==0 (else target->0) -> all absent */
      { 0x01a90040, 0x41a90080, 'I', {0x01a9006b,0x63,0xe7,0x63}, {0,0,0,0}, {0,0,0,0}, 4 },
      /* PlcSrvConnected: OnPlcSrvConnected@0x35260 needs body+32==0 (else target->0) -> all absent */
      { 0x012f0160, 0x012f0180, 'P', {0x012f0024,0x012f006b,0x84}, {0,0,0}, {0,0,0}, 3 },
      /* CmGrantControl (flat 0x20 Data body; codes+sequential offsets from schema table
       * @libGMessageGeo .rodata 0x243d8c): field2 code 0x01cc058b -> body+12 (v3[3]); field4 code
       * 0x01ad -> body+20 (v3[5]). OnCmGrantControl@0x35f50 GRANTS control (HRMENUTREE::ActivateDo +
       * raise the active-state target) only when v3[3]!=0 && v3[5]!=0 -> make field2 & field4 PRESENT=1
       * (gated HEROSCALL_CM_GRANT, default ON; =0 reverts to all-absent for an A/B). */
      { 0x03340040, 0x41cc05e1, 'C', {0x01c20503,0x01c20503,0x01cc058b,0x01cc058b,0x01ad,0xc6},
            {0,0,1,0,1,0}, {0,0,1,0,1,0}, 6 },
    };
    int pi=-1; for(int i=0;i<(int)(sizeof P/sizeof P[0]);i++) if(P[i].req==hdr){ pi=i; break; }
    if(pi<0) return;
    if(P[pi].peer=='C' && !cm_grant){ for(int i=0;i<P[pi].nf;i++) P[pi].pres[i]=0; }  /* A/B: all-absent Cm */
    /* leading GMsgString = reply-to (".QueueHrMmi"), same encoding as INJECT_ACK */
    uint32_t tag=m[4]|(m[5]<<8)|(m[6]<<16)|((uint32_t)m[7]<<24);
    if(tag!=0xe7) return;
    uint32_t nlen=m[8]|(m[9]<<8)|(m[10]<<16)|((uint32_t)m[11]<<24);
    if(nlen==0||nlen>64||12+nlen>size) return;
    char name[80]; memcpy(name,m+12,nlen); name[nlen]=0;
    int rs=q_find_slot(name);
    if(rs<0){
        int best=-1; size_t bestlen=0; int best_n=-1; size_t bestlen_n=0;
        for(int qi=0;qi<MAXQ;qi++){ struct queue*q=&C->queues[qi];
            if(q->used && q->name[0] && strlen(q->name)>=4 && strstr(name,q->name)){
                size_t l=strlen(q->name);
                if(l>bestlen){ bestlen=l; best=qi; }
                if(q->notify_bits && l>bestlen_n){ bestlen_n=l; best_n=qi; } } }
        if(best_n>=0){ best=best_n; }
        if(best>=0){ rs=best;
            LOG("INJECT_PEER_ACK: matched embedded reply queue \"%s\" (notify %08x) in field0 \"%s\"\n",
                C->queues[best].name,C->queues[best].notify_bits,name); }
    }
    if(rs<0){ LOG("INJECT_PEER_ACK: reply queue \"%s\" not found\n",name); return; }
    uint32_t rqid=C->queues[rs].id;
    uint32_t key=(rqid<<8)|(uint8_t)P[pi].peer;
    for(int i=0;i<n_peer_acked;i++) if(peer_acked[i]==key) return;   /* one reply per (peer,reply-queue) */
    if(n_peer_acked<64) peer_acked[n_peer_acked++]=key;
    unsigned char ack[64]; uint32_t p=0; int npres=0;
    put32(ack+p,P[pi].reply); p+=4;                                 /* reply header (e.g. IpoSrvLoginQuit) */
    for(int i=0;i<P[pi].nf;i++){
        if(P[pi].pres[i]){ put32(ack+p,P[pi].code[i]); p+=4; put32(ack+p,P[pi].val[i]); p+=4; npres++; } /* PRESENT [code][value] */
        else { put32(ack+p,0x80000000u|P[pi].code[i]); p+=4; }      /* ABSENT -> field default 0 */
    }
    q_send(rqid,ack,p,0);
    LOG("INJECT_PEER_ACK: posted peer reply 0x%08x (%c, %d fields, %d present) to \"%s\" (0x%x), %u bytes\n",
        P[pi].reply,P[pi].peer,P[pi].nf,npres,C->queues[rs].name,rqid,p);
}

/* INJECT_EVT_ERR (part of the PEER_ACK render-drive set): OnEvtConnected@0x324e0 (success branch) issues
 * an EvtErrorRequest(msg type-id 0x3205C0) poll to QEvtServer and ++the startup request counter (HrModule
 * +59). With no QEvtServer the reply never comes, so the counter never drains to 0 -> the state machine
 * (MoveActiveStateTowardsTarget = the render) never fires. Synthesize the reply EvtAnsErrorRequest
 * (msg/body type-id 0x320841, dispatch->OnEvtAnsErrorRequest@0x34c50):
 *   field0 EvtRequestResult (GMsgEnum, wire code 0x3205eb) PRESENT = 1  -> body+20 (result). The handler
 *          calls HrModule::OneRequestDone ONLY when result==1 (else it re-polls with a fresh EvtErrorRequest).
 *   field1 GMsgList<EvtEvent> PRESENT-empty {code 0x18c, count 0}        -> body+24 (not read for result==1).
 * Post it to the recorded Evt reply queue (the one INJECT_EVT_ACK used = QueueHrMmi 0x30e). */
static int n_evterr_replied=0;
/* The EvtAns is the LAST counter-drainer; its OneRequestDone fires MoveActiveStateTowardsTarget. But the
 * active-state TARGET (HrModule+57) is bootstrapped ONLY by OnHrMmiCfgGlobal (from the 2711B HrMmiCfgGlobal
 * config, msg type-id 0x290081, target = 1 + HandwheelUsesHrMmi). That config comes from ConfigServer (slow,
 * cross-process) and on the wire arrives AFTER the fast in-process injected EvtAns -> the counter drains with
 * target STILL 0 -> Move...Target makes no advance -> no render, and the late target-set never re-fires it.
 * FIX (HEROSCALL_EVTERR_DEFER, default ON): DEFER posting the EvtAns until HrMmi has READ the HrMmiCfgGlobal
 * (released in q_read) so the order is target-set THEN counter-drain -> Activate -> UpdateDisplay -> window. */
static int evterr_defer=-1; static uint32_t deferred_evterr_rqid=0;
static void inject_active_handwheel(uint32_t rqid);   /* render trigger, defined just below */
static void post_evt_ans_error(uint32_t rqid){
    if(n_evterr_replied>=8) return;                                  /* result=1 ends the poll; cap runaway */
    int rs=q_slot(rqid);
    unsigned char ack[32]; uint32_t p=0;
    /* EvtAnsErrorRequest MESSAGE type-id = 0x320601 (EvtAnsErrorRequest::C2Ev GMessage::GMessage(this,0x320601);
     * = the factory key GMessage::ReadMessageRaw looks up AND the dispatch id 3278337==0x320601 routing to
     * OnEvtAnsErrorRequest). NOT 0x320841 — wrong header => factory miss => fmailslotqueue.cpp:324 assert. */
    put32(ack+p,0x00320601u); p+=4;                                  /* EvtAnsErrorRequest header */
    put32(ack+p,0x003205ebu); p+=4; put32(ack+p,1); p+=4;            /* attr0 EvtRequestResult enum (kind 11) present = 1 (result) */
    /* attr1 GMsgList<EvtEvent> (code 0x18c, kind 12 = sub-message list): GMessage::Read@0x3b5a0 case 12 reads
     * [code][count]; EMPTY = count 0 -> GMessage::Initialise(0). (0xFFFFFFFF is the GMsgArray<int> empty form,
     * a different kind, and would make case 12 read 0xFFFFFFFF elements -> parse fail.) */
    put32(ack+p,0x0000018cu); p+=4; put32(ack+p,0); p+=4;
    q_send(rqid,ack,p,0);
    n_evterr_replied++;
    LOG("INJECT_EVT_ERR: posted EvtAnsErrorRequest(result=1) to \"%s\" (0x%x), %u bytes (#%d)\n",
        rs>=0?C->queues[rs].name:"?",rqid,p,n_evterr_replied);
    inject_active_handwheel(rqid);                                  /* render-trigger: standalone CfgActiveHandwheel AFTER the drainer */
}

/* INJECT_ACTIVE_HW (HEROSCALL_INJECT_ACTIVE_HW, default OFF; ON in run_2proc_hrmmi.sh): the render trigger.
 * Called from post_evt_ans_error AFTER the EvtAns (the last counter-drainer) is posted — FIFO order means
 * HrMmi reads the EvtAns first (counter -> 0, Move fires but target still 0 = no advance), THEN this
 * standalone CfgActiveHandwheel (type 0x660801). HrModule::DispatchMessage@0x3d060 routes 0x660801 ->
 * OnCfgActiveHandwheel@0x37580: if GMessage::IsValid(msg,0) it sets the active-state TARGET (HrModule+57)
 * to 1 (or 2 if HandwheelUsesHrMmi) and calls MoveActiveStateTowardsTarget itself. With the counter now 0,
 * Move advances current 0 -> target -> WakeUp/Activate -> HRDATAIF::UpdateDisplay = the FIRST WINDOW. This
 * BYPASSES the incomplete HrMmiCfgGlobal (whose OnHrMmiCfgGlobal bails on a missing/invalid config
 * sub-message BEFORE its own target write at 0x3711d). IsValid(msg,0) = body!=null &&
 * GMsgEntityBody::IsValid(body,false) (a shallow structural check), so an all-default deserialized body
 * passes; body+72 (GMsgArray<HR_TYPE>) defaults to an empty array -> OnCfgActiveHandwheel's copy-ctor is safe.
 * Wire: HEROSCALL_ACTIVE_HW_FILE=<path> replays exact captured bytes (the CfgActiveHandwheel embedded in the
 * 2711B HrMmiCfgGlobal, extracted offline); else synthesize from the schema @libGMessageConfig 0x2a9d48. */
static int inject_active_hw=-1; static int active_hw_done=0;
static void inject_active_handwheel(uint32_t rqid){
    if(inject_active_hw<0){ const char*e=getenv("HEROSCALL_INJECT_ACTIVE_HW"); inject_active_hw=e&&e[0]=='1'; }
    if(!inject_active_hw||active_hw_done) return;
    unsigned char buf[1024]; uint32_t p=0;
    const char*ff=getenv("HEROSCALL_ACTIVE_HW_FILE");
    if(ff&&ff[0]){ FILE*f=fopen(ff,"rb"); if(f){ p=(uint32_t)fread(buf,1,sizeof buf,f); fclose(f);
        LOG("INJECT_ACTIVE_HW: loaded %u wire bytes from %s\n",p,ff); } }
    if(p==0){
        /* synthesize all-default: header + schema attributes (scalar -> absent [0x80000000|code];
         * list/array (66008b/18c/a5) -> present-empty [code][count=0], since an absent top-bit on a
         * list position confuses the deserializer). Codes from the 0x2a9d48 schema, in order. */
        static const uint32_t codes[]={0xe7,0xe7,0x63,0x63,0x66008b,0x66008b,0x1ad,0xe7,0x1ad,
                                       0x66008b,0x1ad,0x84,0x63,0x63,0x63,0x63,0x18c,0xa5,0x18c,0xa5};
        put32(buf+p,0x00660801u); p+=4;
        for(unsigned i=0;i<sizeof codes/sizeof codes[0];i++){
            uint32_t c=codes[i];
            if(c==0x66008b||c==0x18c||c==0xa5){ put32(buf+p,c); p+=4; put32(buf+p,0); p+=4; }
            else { put32(buf+p,0x80000000u|c); p+=4; }
        }
    }
    q_send(rqid,buf,p,0);
    active_hw_done=1;
    int rs=q_slot(rqid);
    LOG("INJECT_ACTIVE_HW: posted CfgActiveHandwheel(0x660801) to \"%s\" (0x%x), %u bytes\n",
        rs>=0?C->queues[rs].name:"?",rqid,p);
}
static void inject_evt_error_reply(uint32_t target_qid,const void*msg,uint32_t size){
    if(!inject_peer_ack||!msg||size<4) return;                       /* gated with PEER_ACK (the render set) */
    int ts=q_slot(target_qid); if(ts<0||strcmp(C->queues[ts].name,"QEvtServer")) return;
    const unsigned char*m=msg;
    uint32_t hdr=(m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24))&0x7fffffff;
    if(hdr!=0x3205c0) return;                                        /* not an EvtErrorRequest poll */
    if(n_evt_acked<=0){ LOG("INJECT_EVT_ERR: no Evt reply queue recorded yet\n"); return; }
    uint32_t rqid=evt_acked_qids[0];
    if(evterr_defer<0){ const char*e=getenv("HEROSCALL_EVTERR_DEFER"); evterr_defer=(!e||e[0]!='0'); } /* default ON */
    if(evterr_defer){
        deferred_evterr_rqid=rqid;                                   /* released in q_read on the HrMmiCfgGlobal read */
        LOG("INJECT_EVT_ERR: DEFERRED EvtAnsErrorRequest to 0x%x until HrMmiCfgGlobal (target bootstrap) is read\n",rqid);
    } else {
        post_evt_ans_error(rqid);
    }
}

/* INJECT_BCAST_ACK (part of the PEER_ACK render-drive set): HrMmi sends GmBroadcastRegisterReq (msg type-id
 * 0x3340261) to AppStartMaster (0x308) to register for broadcasts — each ++the startup request counter and
 * is answered by GmBroadcastRegisterResp (msg type-id 0x43340280; HrModule::DispatchMessage routes 0x43340280
 * straight to OneRequestDone). With no AppStartMaster the reply never comes, so those counted requests never
 * drain. Synthesize the resp (flat-Data 16B body: 3 fields code 0x334020b/0x63/0x63 from the schema table
 * @libGMessageGeo .rodata 0x255e4c; OneRequestDone reads no fields -> all-absent) and post it to the request's
 * reply queue (leading GMsgString reply-to like INJECT_PEER_ACK, else QueueHrMmi). */
static int n_bcast_replied=0;
static void inject_broadcast_register_reply(uint32_t target_qid,const void*msg,uint32_t size){
    (void)target_qid;
    if(!inject_peer_ack||!msg||size<8) return;
    const unsigned char*m=msg;
    uint32_t hdr=(m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24))&0x7fffffff;
    if(hdr!=0x03340261) return;                                     /* not a GmBroadcastRegisterReq */
    if(n_bcast_replied>=8) return;                                  /* HrMmi sends a fixed few; cap runaway */
    int rs=-1;
    uint32_t tag=(size>=8)?(m[4]|(m[5]<<8)|(m[6]<<16)|((uint32_t)m[7]<<24)):0;
    if(tag==0xe7&&size>=12){                                        /* leading GMsgString reply-to */
        uint32_t nlen=m[8]|(m[9]<<8)|(m[10]<<16)|((uint32_t)m[11]<<24);
        if(nlen>0&&nlen<=64&&12+nlen<=size){
            char name[80]; memcpy(name,m+12,nlen); name[nlen]=0;
            rs=q_find_slot(name);
            if(rs<0){ int best=-1; size_t bl=0;
                for(int qi=0;qi<MAXQ;qi++){ struct queue*q=&C->queues[qi];
                    if(q->used&&q->name[0]&&q->notify_bits&&strlen(q->name)>=4&&strstr(name,q->name)){
                        size_t l=strlen(q->name); if(l>bl){bl=l;best=qi;} } }
                rs=best; }
        }
    }
    if(rs<0) rs=q_find_slot("QueueHrMmi");                          /* fallback: HrMmi's main dispatch queue */
    if(rs<0){ LOG("INJECT_BCAST_ACK: no reply queue\n"); return; }
    uint32_t rqid=C->queues[rs].id;
    unsigned char ack[24]; uint32_t p=0;
    put32(ack+p,0x43340280u); p+=4;                                 /* GmBroadcastRegisterResp header */
    put32(ack+p,0x8334020bu); p+=4;                                 /* field0 absent */
    put32(ack+p,0x80000063u); p+=4;                                 /* field1 absent */
    put32(ack+p,0x80000063u); p+=4;                                 /* field2 absent */
    q_send(rqid,ack,p,0);
    n_bcast_replied++;
    LOG("INJECT_BCAST_ACK: posted GmBroadcastRegisterResp to \"%s\" (0x%x), %u bytes (#%d)\n",
        C->queues[rs].name,rqid,p,n_bcast_replied);
}

/* ---------------- regions (named shared memory) ---------------- */
#define REGION_BYTES (64u*1024u*1024u)
static uint32_t reg_ident(const char*name){
    lock();
    for(int i=0;i<MAXREG;i++) if(C->regs[i].used&&!strcmp(C->regs[i].name,name)){
        uint32_t id=C->regs[i].id; unlock(); LOG("M_ident \"%s\" -> 0x%x\n",name,id); return id; }
    int s=-1; for(int i=0;i<MAXREG;i++) if(!C->regs[i].used){ s=i; break; }
    if(s<0){ unlock(); return (uint32_t)-2; }
    C->regs[s].used=1; C->regs[s].id=C->next_reg++; C->regs[s].size=REGION_BYTES;
    strncpy(C->regs[s].name,name,sizeof C->regs[s].name-1);
    uint32_t id=C->regs[s].id; unlock();
    LOG("M_ident \"%s\" -> 0x%x (new)\n",name,id); return id;
}
static void* reg_attach(uint32_t id){
    int s=-1; for(int i=0;i<MAXREG;i++) if(C->regs[i].used&&C->regs[i].id==id){ s=i; break; }
    if(s<0){ LOG("M_attach 0x%x UNKNOWN\n",id); return 0; }
    char path[64]; /* shared file so all procs map the SAME physical region */
    strcpy(path,"/dev/shm/heros_reg_");
    char*p=path+strlen(path); for(const char*q=C->regs[s].name;*q;q++){ char ch=*q; *p++=(ch=='/'||ch==' ')?'_':ch; } *p=0;
    int fd=(int)raw5(SYS_openat,AT_FDCWD,(long)path,O_RDWR|O_CREAT,0600,0);
    if(fd<0) return 0;
    raw5(SYS_ftruncate,fd,C->regs[s].size,0,0,0);
    void*m=mmap(0,C->regs[s].size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,0);
    raw5(SYS_close,fd,0,0,0,0);
    if(m==MAP_FAILED) return 0;
    LOG("M_attach \"%s\" 0x%x -> %p\n",C->regs[s].name,id,m);
    return m;
}

/* ---------------- env ---------------- */
static int env_get(const char*name,char*out,uint32_t sz){
    const char*v=getenv(name);
    if(!v){ if(out&&sz) out[0]=0; return 0; }
    size_t L=strlen(v);
    if(L>=sz){ if(sz){ memcpy(out,v,sz-1); out[sz-1]=0; } } else memcpy(out,v,L+1);
    LOG("Sys_getenv \"%s\" -> \"%s\"\n",name,v);
    return 0;
}

/* Lazy, idempotent init — runs on the first heroscall in each process (the
 * LD_PRELOAD constructor does not reliably fire under the explicit-loader
 * invocation, and each fork+exec'd child must re-attach the shared segment). */
static volatile int g_inited=0, g_guard=0;
static void ensure_init(void){
    if(__atomic_load_n(&g_inited,__ATOMIC_ACQUIRE)) return;
    while(__atomic_exchange_n(&g_guard,1,__ATOMIC_ACQUIRE)) ;
    if(!__atomic_load_n(&g_inited,__ATOMIC_ACQUIRE)){
        const char *v=getenv("HEROSCALL_VERBOSE"); vrb=v&&v[0]=='1';
        const char *bt=getenv("HEROSCALL_BTRACE"); btrace_on=bt&&bt[0]=='1';
        const char *hs=getenv("HEROSCALL_HSTRACE"); hstrace=hs&&hs[0]=='1';
        const char *ht=getenv("HEROSCALL_HSTRACE_TASKS");
        if(ht){ hst_ntasks=0; const char*q=ht;
            while(*q&&hst_ntasks<16){ uint32_t v2=0; int any=0;
                while((*q>='0'&&*q<='9')||(*q>='a'&&*q<='f')||(*q>='A'&&*q<='F')){
                    int d=(*q<='9')?*q-'0':((*q|0x20)-'a'+10); v2=v2*16+(uint32_t)d; q++; any=1; }
                if(any) hst_tasks[hst_ntasks++]=v2;
                while(*q && !((*q>='0'&&*q<='9')||(*q>='a'&&*q<='f')||(*q>='A'&&*q<='F'))) q++; } }
        const char *ad=getenv("HEROSCALL_AS_DELIVER"); as_deliver=!(ad&&ad[0]=='0');
        const char *aq=getenv("HEROSCALL_AUTO_QUEUE"); q_autocreate=!(aq&&aq[0]=='0');
        const char *qn=getenv("HEROS_QIDENT_NOTIFY"); qident_notify=!(qn&&qn[0]=='0');
        const char *qw=getenv("HEROSCALL_QREAD_MAXWAIT"); qread_maxwait=0;
        if(qw) for(const char*q=qw; *q>='0'&&*q<='9'; q++) qread_maxwait=qread_maxwait*10+(uint32_t)(*q-'0');
        const char *st=getenv("HEROSCALL_SYNC_TIMEOUT"); sync_timeout=0;
        if(st) for(const char*q=st; *q>='0'&&*q<='9'; q++) sync_timeout=sync_timeout*10+(uint32_t)(*q-'0');
        const char *si=getenv("HEROSCALL_SEM_INIT"); if(si){ sem_autocount=0;
            for(const char*q=si; *q>='0'&&*q<='9'; q++) sem_autocount=sem_autocount*10+(*q-'0'); }
        const char *dq=getenv("HEROSCALL_DUMPQ"); qdump=dq&&dq[0]=='1';
        const char *hw=getenv("HEROSCALL_HWS_STUB"); hws_stub=hw&&hw[0]=='1';
        const char *tf=getenv("HEROSCALL_TIMERS"); timers_fire=tf&&tf[0]=='1';
        const char *rt=getenv("HEROSCALL_REPLAY_TRIGGER"); replay_trigger=rt&&rt[0]=='1';
        const char *iu=getenv("HEROSCALL_INJECT_UPD"); inject_upd=iu&&iu[0]=='1';
        const char *ia=getenv("HEROSCALL_INJECT_ACK"); inject_ack=ia&&ia[0]=='1';
        const char *iea=getenv("HEROSCALL_INJECT_EVT_ACK"); inject_evt_ack=iea&&iea[0]=='1';
        const char *ipa=getenv("HEROSCALL_INJECT_PEER_ACK"); inject_peer_ack=ipa&&ipa[0]=='1';
        const char *ir=getenv("HEROSCALL_INJECT_REREAD"); inject_reread=ir&&ir[0]=='1';
        const char *ep=getenv("HEROS_EVENTS_PIPE"); events_bridge=ep&&ep[0]=='1';
        ctl_init();
        __atomic_store_n(&g_inited,1,__ATOMIC_RELEASE);
    }
    __atomic_store_n(&g_guard,0,__ATOMIC_RELEASE);
}
__attribute__((constructor)) static void rtos_init(void){ ensure_init(); }

/* ---- /dev/events bridge hooks, called by herosapi_shim (weak-linked) ----
 * herosapi_shim owns the /dev/events open()/ioctl() interpose; it hands us the per-thread
 * pipe (rd,wr) + the enabled sysevent mask so ev_send/ev_receive can wake/drain the right
 * select(). Exported (default visibility) so herosapi_shim's weak ref resolves at runtime. */
void heros_evdev_register(int rd_fd,int wr_fd){
    ensure_init();
    if(!events_bridge) return;
    uint32_t t=task_self();
    lock();
    int i; for(i=0;i<n_evdev;i++) if(evdevs[i].rd==rd_fd||evdevs[i].task==t) break;  /* replace on re-open */
    if(i>=MAXEVDEV){ unlock(); return; }
    if(i==n_evdev) n_evdev++;
    evdevs[i].task=t; evdevs[i].rd=rd_fd; evdevs[i].wr=wr_fd; evdevs[i].sysmask=0; evdevs[i].signaled=0;
    struct evdev *ep=&evdevs[i];
    unlock();
    LOG("evdev register task 0x%x rd=%d wr=%d\n",t,rd_fd,wr_fd);
    evdev_start_watcher(ep);   /* cross-process /dev/events wake (Guppy->skmgr softkey-login notify) */
}
void heros_evdev_setmask(int rd_fd,unsigned int mask){
    ensure_init();
    if(!events_bridge) return;
    lock();
    for(int i=0;i<n_evdev;i++) if(evdevs[i].rd==rd_fd){ evdevs[i].sysmask=mask;
        evdev_reconcile_locked(&evdevs[i]); break; }
    unlock();
}

/* ---------------- opt-in crash fault-locator (HEROSCALL_BTRACE=1) -------------
 * The box has no gdb/strace, and glibc backtrace()/dlsym pull GLIBC_2.34 (the
 * control's glibc is 2.31). So: interpose sigaction(), forward via RAW
 * rt_sigaction with our own restorer trampoline (no dlsym), and on a fatal signal
 * print the faulting EIP + address + /proc/self/maps so EIP can be mapped to a
 * lib+offset (and thence a function, offline) — no glibc backtrace needed. */
static struct { int set; void(*h)(int); void(*sa)(int,siginfo_t*,void*); int flags; } ctrl_h[40];
static int is_fatal(int s){ return s==SIGSEGV||s==SIGABRT||s==SIGBUS||s==SIGILL||s==SIGFPE; }
/* i386 signal restorer: handlers return through here -> rt_sigreturn */
__attribute__((naked)) static void rtos_restorer(void){ __asm__("movl $173,%eax; int $0x80"); }
/* kernel rt_sigaction(sig, kact, koldact, 8). kernel act = {handler,flags,restorer,mask[2]} */
static int kern_sigaction(int sig,void*h,unsigned long flags,unsigned long m0,unsigned long m1,int wrap){
    unsigned long ka[5]={ (unsigned long)h, flags|0x04000000/*SA_RESTORER*/|(wrap?0x00000004/*SA_SIGINFO*/:0),
                          (unsigned long)rtos_restorer, m0, m1 };
    return (int)raw5(SYS_rt_sigaction,sig,(long)ka,0,8,0);
}
static void hx(unsigned long v){ char b[11]="0x00000000"; for(int i=0;i<8;i++){ int d=(v>>((7-i)*4))&0xf; b[2+i]=d<10?'0'+d:'a'+d-10; } raw5(SYS_write,2,(long)b,10,0,0); }
static void crash_locate(int sig,siginfo_t*si,void*ucv){
    ucontext_t*uc=(ucontext_t*)ucv;
    raw5(SYS_write,2,(long)"\n=== [rtos] FAULT sig=",22,0,0); hx(sig);
    raw5(SYS_write,2,(long)" eip=",5,0,0); hx(uc?(unsigned long)uc->uc_mcontext.gregs[REG_EIP]:0);
    raw5(SYS_write,2,(long)" addr=",6,0,0); hx(si?(unsigned long)si->si_addr:0);
    /* EBP-chain walk + raw stack dump from ESP (catches the smashing function even
     * when glibc's abort path omits frame pointers — correlate addrs with maps). */
    raw5(SYS_write,2,(long)"\n frames:",9,0,0);
    if(uc){ unsigned long bp=(unsigned long)uc->uc_mcontext.gregs[REG_EBP];
        for(int i=0;i<10&&bp>0x1000&&bp<0xfffff000;i++){
            raw5(SYS_write,2,(long)" ",1,0,0); hx(*(unsigned long*)(bp+4));
            unsigned long nb=*(unsigned long*)bp; if(nb<=bp) break; bp=nb; } }
    raw5(SYS_write,2,(long)"\n stack:",8,0,0);
    if(uc){ unsigned long sp=(unsigned long)uc->uc_mcontext.gregs[REG_ESP];
        for(int i=0;i<96&&sp;i++){ raw5(SYS_write,2,(long)" ",1,0,0); hx(*(unsigned long*)(sp+4*i)); } }
    raw5(SYS_write,2,(long)"\n--- /proc/self/maps ---\n",25,0,0);
    int fd=(int)raw5(SYS_openat,AT_FDCWD,(long)"/proc/self/maps",0,0,0);
    if(fd>=0){ char buf[1024]; long r; while((r=raw5(SYS_read,fd,(long)buf,sizeof buf,0,0))>0) raw5(SYS_write,2,(long)buf,r,0,0); raw5(SYS_close,fd,0,0,0,0); }
    raw5(SYS_write,2,(long)"=== [rtos] end fault ===\n",25,0,0);
    if(sig>0&&sig<40&&ctrl_h[sig].set){  /* chain to control's handler */
        if((ctrl_h[sig].flags&SA_SIGINFO)&&ctrl_h[sig].sa) ctrl_h[sig].sa(sig,si,ucv);
        else if(ctrl_h[sig].h) ctrl_h[sig].h(sig); }
    raw5(SYS_exit_group,128+sig,0,0,0,0);
}
static void chk_btrace(void){ static int done=0; if(done)return; done=1;
    const char*b=getenv("HEROSCALL_BTRACE"); btrace_on=(b&&b[0]=='1'); }
int sigaction(int sig,const struct sigaction*act,struct sigaction*old){
    chk_btrace();
    if(act&&btrace_on&&is_fatal(sig)){
        ctrl_h[sig].set=1; ctrl_h[sig].flags=act->sa_flags;
        ctrl_h[sig].h=act->sa_handler; ctrl_h[sig].sa=act->sa_sigaction;
        const unsigned long*m=(const unsigned long*)&act->sa_mask;
        return kern_sigaction(sig,(void*)crash_locate,act->sa_flags,m[0],m[1],1);
    }
    if(act){ const unsigned long*m=(const unsigned long*)&act->sa_mask;
        return kern_sigaction(sig,(void*)act->sa_handler,act->sa_flags,m[0],m[1],
                              (act->sa_flags&SA_SIGINFO)!=0); }
    return (int)raw5(SYS_rt_sigaction,sig,0,(long)old,8,0);
}
/* signal(): older API some HeROS code uses to install handlers */
void (*signal(int sig,void(*h)(int)))(int){
    chk_btrace();
    if(btrace_on&&is_fatal(sig)&&h!=SIG_DFL&&h!=SIG_IGN){
        ctrl_h[sig].set=1; ctrl_h[sig].flags=0; ctrl_h[sig].h=h; ctrl_h[sig].sa=0;
        kern_sigaction(sig,(void*)crash_locate,0,0,0,1);
        return (void(*)(int))0;
    }
    kern_sigaction(sig,(void*)h,0,0,0,0);
    return (void(*)(int))0;
}

long syscall(long n,...){
    va_list ap; va_start(ap,n);
    long a=va_arg(ap,long),b=va_arg(ap,long),c=va_arg(ap,long),d=va_arg(ap,long),e=va_arg(ap,long);
    va_end(ap);
    if(n!=222) return raw5(n,a,b,c,d,e);

    ensure_init();
    uint32_t cmd=(uint32_t)a; int lo=cmd&0xff; uint32_t *p=(uint32_t*)b;
    if(vrb) fprintf(stderr,"[t%ld hc %02x %-11s] p=[%08x %08x %08x %08x] a2=%lx\n",
        raw5(SYS_gettid,0,0,0,0,0),lo,hcname(lo),p?p[0]:0,p?p[1]:0,p?p[2]:0,p?p[3]:0,c);
    if(vrb&&p&&(lo==0x00||lo==0x02||lo==0x0a||lo==0x0b||lo==0x0d||lo==0x0e)){
        fprintf(stderr,"   FULL[%02x]:",lo);
        for(int i=0;i<14;i++) fprintf(stderr," %08x",p[i]); fprintf(stderr,"\n"); }
    switch(lo){
    case 0x00:{ /* T_create (task side, sub_D330): register + block until t_start delivers ctx.
                 * param: p[2]=msgsize, p[6]=&arg_out, p[8]=&taskid_out, p[10]=ctx_buf, p[12]=parent */
        if(!p) return -1;
        uint32_t T=task_self(); int s=task_slot(T);
        if(s>=0){ C->tasks[s].ctx_dst=p[10]; C->tasks[s].arg_dst=p[6];
                  C->tasks[s].msgsize=p[2]; __atomic_store_n(&C->tasks[s].started,0,__ATOMIC_RELEASE); }
        if(p[8]) *(uint32_t*)(uintptr_t)p[8]=T;        /* deliver task id to the parent (arg[4]) */
        if(p[12] && task_slot(p[12])>=0){
            ev_send(p[12],0x80000);                    /* wake t_create_ex's ev_receive(0x80000) */
        } else {
            /* MAIN-CONTEXT / orphan t_create: the parent id is 0xffffffff (winmgr's
             * FThread::CreateMainContext runs in the bootstrap thread BEFORE libheros knows that
             * thread's heros task id, so it passes parent=-1). The direct ev_send above would be
             * lost -> the parent's t_create rendezvous (ev_receive(0x80000)) never completes ->
             * t_create returns -1 -> "THREAD: Operating system could not create thread" -> the
             * whole subsystem (winmgr) never reaches WmModule::Initialize. FIX: publish an orphan
             * rendezvous token + futex-wake every thread currently blocked waiting on the 0x80000
             * rendezvous bit so the real parent (whoever it is) re-checks and proceeds. 0x80000 is
             * the dedicated t_create wake bit, so only a t_create parent ever waits on it. */
            __atomic_add_fetch(&C->orphan_tc,1,__ATOMIC_ACQ_REL);
            lock();
            for(int i=0;i<MAXTASK;i++) if(C->tasks[i].used && (C->tasks[i].last_ev_want & 0x80000u))
                futex((void*)&C->tasks[i].events,FUTEX_WAKE,0x7fffffff,0);
            unlock();
            LOG("T_create ORPHAN task 0x%x parent 0x%x -> published rendezvous token (orphan_tc=%u)\n",
                T,p[12],__atomic_load_n(&C->orphan_tc,__ATOMIC_ACQUIRE));
        }
        LOG("T_create task 0x%x parent 0x%x ctxbuf %08x -> wait t_start\n",T,p[12],p[10]);
        HST(T,p[12],"TC t%x parent=t%x -> block until T_start\n",T,p[12]);
        if(s>=0){ for(;;){ uint32_t st=__atomic_load_n(&C->tasks[s].started,__ATOMIC_ACQUIRE);
                  if(st) break; futex(&C->tasks[s].started,FUTEX_WAIT,0,0); } }
        return 0;                                       /* success -> sub_D330 calls _run(arg_out, ctx_buf) */
    }
    case 0x02:{ /* T_start (parent): deliver context to task & resume it.
                 * param: p[0]=size, p[2]=ctx src, p[4]=task id */
        if(!p) return -1;
        uint32_t tid=p[4],size=p[0],src=p[2]; int s=task_slot(tid);
        if(s<0){ LOG("T_start unknown task 0x%x\n",tid); return -1; }
        uint32_t cap=C->tasks[s].msgsize; uint32_t cp=size; if(cap&&cp>cap) cp=cap; /* never exceed the task's buffer */
        if(C->tasks[s].ctx_dst&&src&&cp) memcpy((void*)(uintptr_t)C->tasks[s].ctx_dst,(void*)(uintptr_t)src,cp);
        if(C->tasks[s].arg_dst) *(uint32_t*)(uintptr_t)C->tasks[s].arg_dst=size;
        __atomic_store_n(&C->tasks[s].started,1,__ATOMIC_RELEASE);
        futex(&C->tasks[s].started,FUTEX_WAKE,0x7fffffff,0);
        LOG("T_start task 0x%x size %u (buf %u, copied %u) -> resumed\n",tid,size,cap,cp);
        HST(tid,task_self(),"TS t%x resumed by t%x (ctx %u bytes)\n",tid,task_self(),cp);
        return 0;                                       /* >=0 -> t_start() returns true (success) */
    }
    case 0x01:{ /* T_ident: name inline p[0..1] (0=self) */
        if(p&&(p[0]||p[1])){
            char nm[9]; memcpy(nm,p,8); nm[8]=0;
            uint32_t id=task_by_name(nm); if(id) return id;
        }
        return (long)task_self();
    }
    case 0x09:{ /* T_name: set current task name (p[0]=name ptr) */
        if(p&&p[0]){ uint32_t self=task_self(); int s=task_slot(self);
            if(s>=0){ lock(); strncpy(C->tasks[s].name,(const char*)(uintptr_t)p[0],NAMELEN-1); unlock(); } }
        return 0;
    }
    case 0x10: /* Ev_send(target@p[0], bits@p[1]) */
        if(p) return ev_send(p[0],p[1]);
        return -7;
    case 0x11: /* Ev_receive(want@p[0], cond@p[1], timeout@p[2]) -> event word */
        if(p) return (long)(int32_t)ev_receive(p[0],p[1],p[2]);
        return 0;
    case 0x12: /* As_send(target@p[0], bits@p[1]) */
        if(p) return as_send(p[0],p[1]);
        return -9;
    case 0x13: /* As_mask(valptr@p[0], op@p[2]) — writes result back through *p[0] */
        if(p) return as_mask_op((uint32_t*)(uintptr_t)p[0], p[2]);
        return -22;
    case 0x14: /* As_read(reqptr@p[0], outptr@p[2]) — drains caught async signals */
        if(p) return as_read((uint32_t*)(uintptr_t)p[0],(uint32_t*)(uintptr_t)p[2]);
        return -22;
    case 0x15: /* Sm_create(name@p[0], count@p[2], flags@p[3]) -> sem id */
        if(p&&p[0]) return (long)sem_make((const char*)(uintptr_t)p[0], (int)p[2]);
        if(p) return (long)sem_make(0, (int)p[2]);
        return (long)sem_make(0,0);
    case 0x16: /* Sm_ident(name@p[0]) */
        if(p&&p[0]){ uint32_t id=sem_ident((const char*)(uintptr_t)p[0]); return id?(long)id:-2; }
        return -2;
    case 0x18: /* Sm_request(id@p[0], flags@p[1], timeout@p[2]) */
        if(p) return sem_request(p[0], p[2]);
        return -7;
    case 0x19: /* Sm_release(id@p[0]) */
        if(p) return sem_release(p[0]);
        return -7;
    case 0x0a: /* Q_create(name@p[0], depth@p[2], flags@p[3]) -> queue id */
        if(p&&p[0]) return (long)(int32_t)q_create((const char*)(uintptr_t)p[0], p[2], p[3]);
        return (long)(int32_t)q_create("", 0, 0);
    case 0x0b: /* Q_ident(name@p[0]) -> queue id (-1 if not found).
                * NOT-FOUND IS -1 (0xffffffff), the heros convention: most consumers
                * sign-test the result, but some EXACT-compare `== 0xffffffff` — notably
                * FMailslotQueue::TemporaryQueuename (libbackend @0x21eb6), which mints a
                * fresh temp mailslot by scanning "HwsM<task>N<ctr>" until q_ident returns
                * -1 (free). Returning -0x13 made every name look "taken" → infinite scan.
                * q_ident() still black-holes absent fire-and-forget sinks (QEvtServer, …)
                * so strict FMailslotQueue::Write succeeds, but reports presence-probed names
                * (QueueHeLogger, HwsM* temp slots) as not-found. */
        if(p&&p[0]){ uint32_t id=q_ident((const char*)(uintptr_t)p[0]); return id?(long)id:-1; }
        LOG("Q_ident EMPTY/NULL name (p0=%08x) -> -1 (reply will black-hole)\n", p?p[0]:0);
        return -1;
    case 0x0d:{ /* Q_send(msg@p[0], size@p[2], qid@p[4], mode@p[6]) */
        if(!p) return -9;
        int r=q_send(p[4], (const void*)(uintptr_t)p[0], p[2], p[6]);
        hws_autoreply(p[4], (const void*)(uintptr_t)p[0], p[2]);   /* HWS_STUB: synth QHWServer reply */
        inject_connect_ack(p[4], (const void*)(uintptr_t)p[0], p[2]); /* INJECT_ACK: synth IPO connect-ACK */
        inject_evt_connect_ack(p[4], (const void*)(uintptr_t)p[0], p[2]); /* INJECT_EVT_ACK: synth QEvtServer connect-ACK */
        inject_peer_connect_ack(p[4], (const void*)(uintptr_t)p[0], p[2]); /* INJECT_PEER_ACK: synth IPO/PLC/CM connect replies */
        inject_evt_error_reply(p[4], (const void*)(uintptr_t)p[0], p[2]); /* INJECT_EVT_ERR: answer the EvtErrorRequest poll (drain counter) */
        inject_broadcast_register_reply(p[4], (const void*)(uintptr_t)p[0], p[2]); /* INJECT_BCAST_ACK: answer GmBroadcastRegisterReq (drain counter) */
        return r;
    }
    case 0x0e: /* Q_read(outbuf@p[0], maxsize@p[2], hdr/base@p[4], timeout@p[6], qid@p[7]).
                * p[4]!=0 (q_receive) -> kernel writes a 12-byte sender header there. */
        if(p) return q_read(p[7], (void*)(uintptr_t)p[0], p[2], p[6], (uint32_t*)(uintptr_t)p[4]);
        return -9;
    case 0x1a:{ /* Tm_wkafter(@p[0]) — sleep, then return 0. The arg UNIT is ambiguous (HeROS ev-timers
                 * are microseconds — cf. Tm_evafter "delay_us"); winmgr passes 0x989680 (=10s if us, but
                 * =10000s if treated as ms) as a startup delay. Treating it as ms hangs winmgr for ~2.7h.
                 * CAP the sleep at TM_WKAFTER_CAP_MS (default 3000ms) so a long startup delay can't stall
                 * a bounded run — harmless (only SHORTENS long sleeps; short ConfigServer/HrMmi values are
                 * unaffected, both tolerated this path before). */
        if(p&&p[0]){ unsigned long ms=p[0]/1000;
            static long cap=-1; if(cap<0){ const char*e=getenv("HEROSCALL_TM_WKAFTER_CAP_MS"); cap=e?atol(e):3000; }
            if(cap>0 && ms>(unsigned long)cap) ms=(unsigned long)cap;
            struct timespec ts={(long)(ms/1000),(long)(ms%1000)*1000000L};
            raw5(SYS_nanosleep,(long)&ts,0,0,0,0); }
        return 0;
    }
    /* (no explicit Tm_check/0x31 handler: returning 0 ABORTS ConfigServer's timer logic (signal 6);
     * the default unhandled path is what both ConfigServer and HrMmi tolerated, so leave 0x31 to it.) */
    case 0x1b:   /* Tm_evafter(delay_us@p[0], event_bits@p[1]) — fire event to CALLER after delay  */
    case 0x1d:{  /* Tm_evevery(period_us@p[0], event_bits@p[1]) — periodic.  Kernel: Tm_create →
                  * __usecs_to_jiffies(p[0]) on GET_TASK_CURRENT(), sends p[1] on expiry. Without
                  * firing, the pending-client-msg flush timer never delivers → no connect-ACKs.
                  * v1: fire IMMEDIATELY (delay ignored) — the dispatch re-arms only while
                  * PostClientMsg::HasPendings(), so this drains pendings and converges (no busy
                  * loop). Proper delayed/periodic firing via a timer thread = TODO. */
        if(timers_fire && p){
            uint32_t self=task_self(); ev_send(self, p[1]);
            LOG("Tm_ev%s delay=%u us bits=%08x -> ev_send(0x%x) [immediate]\n",
                lo==0x1b?"after":"every", p?p[0]:0, p?p[1]:0, self);
            return 1;                       /* nonzero fake timer id */
        }
        return 0;
    }
    case 0x1f: return 0;   /* Tm_cancel(timer id) — no-op (our timers are one-shot immediate) */
    case 0x22: /* M_ident(name@p[0]) */
        if(p&&p[0]) return (long)(int32_t)reg_ident((const char*)(uintptr_t)p[0]);
        return -2;
    case 0x23: /* M_attach(region id@p[2]) */
        if(p) return (long)(intptr_t)reg_attach(p[2]);
        return 0;
    case 0x27: /* Sys_getenv(name@p[0], outbuf@p[2], size@p[4]) */
        if(p&&p[0]) return env_get((const char*)(uintptr_t)p[0],(char*)(uintptr_t)p[2],p[4]);
        return -2;
    case 0x29: /* P_ident(name): look up a RUNNING process by name. The emulator tracks tasks/queues,
                * not P_ processes, so report NOT-FOUND (-1) — else AppStart::Processes::OnMessage(FmLoadProcess)
                * reads p_ident!=-1 as "process already running" and throws "...twice" BEFORE IsAFile/PCreate,
                * silently aborting the spawn (the default 0 broke the fork). -1 = heros not-found convention. */
        { const char*pn=(p&&p[0])?(const char*)(uintptr_t)p[0]:"?";
          if(strstr(pn,"winmgr")||strstr(pn,"mgr")) { fprintf(stderr,"[rtos] P_ident(\"%s\") -> -1 (Processes::OnMessage reached)\n",pn); fflush(stderr); }
          else LOG("P_ident(\"%s\") -> -1\n",pn); }
        return (uint32_t)-1;
    default:
        return 0;   /* T_create,T_start,As,P_childstat,P_signal,Tm etc — success stub for now */
    }
}

static const char* hcname(int lo){ switch(lo){
  case 0x01:return"T_ident";case 0x02:return"T_start";case 0x03:return"T_delete";
  case 0x07:return"T_getargs";case 0x08:return"T_setname";case 0x09:return"T_name";
  case 0x0a:return"Q_create";case 0x0b:return"Q_ident";case 0x0d:return"Q_send";
  case 0x0e:return"Q_read";case 0x10:return"Ev_send";case 0x11:return"Ev_receive";
  case 0x12:return"As_send";case 0x13:return"As_mask";case 0x14:return"As_read";
  case 0x15:return"Sm_create";case 0x16:return"Sm_ident";
  case 0x18:return"Sm_request";case 0x19:return"Sm_release";case 0x1a:return"Tm_wkafter";
  case 0x1b:return"Tm_evafter";case 0x1d:return"Tm_evevery";case 0x1f:return"Tm_cancel";
  case 0x21:return"M_create";case 0x22:return"M_ident";case 0x23:return"M_attach";
  case 0x24:return"M_detach";case 0x26:return"Sys_setenv";case 0x27:return"Sys_getenv";
  case 0x29:return"P_ident";case 0x2a:return"P_childstat";case 0x2b:return"P_signal";
  case 0x2c:return"P_setname";case 0x2d:return"P_name";case 0x32:return"Sys_control";
  case 0x33:return"Q_name";default:return"?";} }
