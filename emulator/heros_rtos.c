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
              volatile uint32_t last_ev_want; };     /* most recent Ev_receive want-mask (for queue-notify bit matching) */
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

struct ctl {
    volatile int magic;                              /* 0=empty 1=initing 2=ready */
    volatile int32_t lock;                           /* futex spinlock */
    volatile uint32_t next_task, next_sem, next_q, next_reg;
    struct task   tasks[MAXTASK];
    struct sem    sems[MAXSEM];
    struct queue  queues[MAXQ];
    struct region regs[MAXREG];
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
    /* FModule queue-pending fix (send-before-wait): if this task is about to wait on a SINGLE bit
     * and already OWNS a queue with pending messages whose flags-notify bit is NOT that bit, then
     * that bit is the queue's real FWaitable id (GetUniqueEventId) — deliver it so the owner reads
     * its queue now (the logo thread: waits 0x1000 = queue 0x313's id, but Q_send notified the
     * flags-byte 0x01000000 BEFORE the wait existed). Self-limits: returns until the queue drains,
     * then waits normally. ConfigServer/IPO unaffected (their queue bit overlaps their want). */
    if(want && (want&(want-1))==0){
        for(int qi=0; qi<MAXQ; qi++){ struct queue*q=&C->queues[qi];
            if(q->used && q->owner==self && q->head!=q->tail && (q->notify_bits&want)==0){
                LOG("EV queue-pending: task 0x%x owns q 0x%x (pending) -> deliver want %08x\n",self,q->id,want);
                return want; } }
    }
    int synthetic=0;
    if(ev_unblock_ms==-2){ const char*e=getenv("HEROSCALL_EV_UNBLOCK_MS"); ev_unblock_ms=e?atoi(e):-1; }
    if(!ev_inject_init){ ev_inject_init=1;
        const char*w=getenv("HEROSCALL_EV_INJECT_WANT"); ev_inject_want=w?(uint32_t)strtoul(w,0,16):0;
        const char*b=getenv("HEROSCALL_EV_INJECT_BIT");  ev_inject_bit =b?(uint32_t)strtoul(b,0,16):0; }
    if(timeout==0xffffffff && ev_unblock_ms>0){
        if(!ev_inject_want || want==ev_inject_want){ timeout=(uint32_t)ev_unblock_ms; synthetic=1; }
    }
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
    for(;;){
        uint32_t cur=__atomic_load_n(ev,__ATOMIC_ACQUIRE);
        int ok = cond==1 ? ((cur&want)==want) : ((cur&want)!=0);
        if(ok){
            uint32_t caught = cur & want;
            __atomic_and_fetch(ev,~caught,__ATOMIC_ACQ_REL);  /* consume */
            return caught;
        }
        if(timeout==0) return 0;
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
    __atomic_or_fetch(&C->tasks[s].events,bits,__ATOMIC_ACQ_REL);
    futex(&C->tasks[s].events,FUTEX_WAKE,0x7fffffff,0);
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
static uint32_t qread_maxwait=0;     /* HEROSCALL_QREAD_MAXWAIT=ms caps "forever" Q_read waits (debug) */
static uint32_t sync_timeout=0;      /* HEROSCALL_SYNC_TIMEOUT=ms caps forever Q_read on "*Sync" handshake
                                      * queues only (e.g. QSikSync) — these deadlock when their server peer
                                      * isn't running (no external SikServer); legit fast replies still pass */
static int sem_autocount=1;          /* HEROSCALL_SEM_INIT=n: initial count for auto-created sems */
static int qdump=0;                  /* HEROSCALL_DUMPQ=1: hex-dump queue message payloads */
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
static volatile int runup_done=0;        /* set when the HWS stub fires (= run-up complete) */
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
    if(!qdump||!p) return; if(n>112)n=112;
    fprintf(stderr,"   %s[0x%x]:",tag,id);
    for(uint32_t k=0;k<n;k++) fprintf(stderr,"%02x",((const uint8_t*)p)[k]); fprintf(stderr,"\n");
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
static uint32_t q_create(const char*nm,uint32_t depth,uint32_t flags){
    char base[NAMELEN]; q_basename(base,nm);
    uint32_t owner=task_self();                          /* creator owns the queue (kernel +0xb8) */
    uint32_t nbits=(flags&2)?(flags&0xff000000u):0;      /* flag bit1 => notify; bits = top byte (kernel +0xe8) */
    lock();
    int s=q_find_slot(base);
    if(s>=0){ uint32_t id=C->queues[s].id; unlock(); return id; }   /* idempotent on name */
    s=-1; for(int i=0;i<MAXQ;i++) if(!C->queues[i].used){ s=i; break; }
    if(s<0){ unlock(); LOG("Q_create: table full\n"); return 0; }
    C->queues[s].used=1; C->queues[s].id=C->next_q++; C->queues[s].head=C->queues[s].tail=0;
    C->queues[s].depth=depth; C->queues[s].flags=flags;
    C->queues[s].owner=owner; C->queues[s].notify_bits=nbits;
    C->queues[s].name[0]=0; strncpy(C->queues[s].name,base,NAMELEN-1);
    uint32_t id=C->queues[s].id; unlock();
    LOG("Q_create \"%s\" depth %u flags %x owner 0x%x notify %08x -> 0x%x\n",base,depth,flags,owner,nbits,id);
    return id;
}
/* Names used as service PRESENCE PROBES: auto-creating these defeats the control's
 * graceful "service absent" degradation. e.g. HeLogging::CheckHeloggerIsRunning does
 * q_ident("QueueHeLogger") and, on failure, logs locally instead of blocking on the
 * logger handshake (ConnectToHelogger). So these must report "not found". */
static int q_is_probe_name(const char*base){
    static const char*const probe[]={ "QueueHeLogger", 0 };
    for(int i=0;probe[i];i++) if(!strcmp(base,probe[i])) return 1;
    /* HwsMailslot transient per-instance mailslots ("HwsM<task>N<ctr>"): the control
     * SCANS these by incrementing the counter, stopping when Q_ident reports "not
     * found" (a peer server owns the real ones). Auto-creating each as a black hole
     * makes the scan never terminate — an unbounded loop (120MB+ log, starves peers
     * on the global lock). Report absent so the scan ends. */
    if(!strncmp(base,"HwsM",4)) return 1;
    return 0;
}
static uint32_t q_ident(const char*nm){
    char base[NAMELEN]; q_basename(base,nm);
    lock(); int s=q_find_slot(base); uint32_t id=(s>=0)?C->queues[s].id:0; unlock();
    if(!id && q_autocreate && !q_is_probe_name(base)){    /* black-hole sink for absent peers */
        id=q_create(base,2,0); LOG("Q_ident \"%s\" -> auto 0x%x\n",base,id); return id;
    }
    LOG("Q_ident \"%s\" -> 0x%x\n",base,id);
    return id;                                            /* 0 => not found */
}
static int q_send(uint32_t id,const void*msg,uint32_t size,uint32_t mode){
    int s=q_slot(id); if(s<0){ LOG("Q_send unknown queue 0x%x size %u\n",id,size); qhex("Q_FAIL",id,msg,size); return -9; }
    if(size>QMSGCAP){ LOG("Q_send size %u > cap %u, truncating\n",size,QMSGCAP); size=QMSGCAP; }
    uint32_t sender=task_self();
    struct queue*q=&C->queues[s];
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
    __atomic_add_fetch(&q->tail,1,__ATOMIC_ACQ_REL);
    uint32_t owner=q->owner, nbits=q->notify_bits;
    /* FModule notify-bit fix: notify_bits is the flags-derived top byte (0x01000000), but a module
     * registers its input queue under a per-waitable id (FWaitable::GetUniqueEventId) = the bit it
     * actually Ev_receive-waits for. If that flags-bit isn't in the owner's current single-bit wait,
     * notify THAT bit so the owner wakes (AppStartMP "logo" queue: flags->0x01000000, logo waits
     * 0x1000). Overlap cases (ConfigServer 0x01000000 in its 0x01011000 wait) are unchanged. */
    if(nbits&&owner){ int os=task_slot(owner); uint32_t ow=(os>=0)?C->tasks[os].last_ev_want:0;
        if(ow && (nbits&ow)==0 && (ow&(ow-1))==0) nbits=ow; }
    unlock();
    futex(&q->tail,FUTEX_WAKE,0x7fffffff,0);          /* wake any Q_read blocker (kernel __wake_up) */
    if(nbits&&owner) ev_send(owner,nbits);            /* event-driven serve loop (kernel Ev_sendtcb +0xb8/+0xe8) */
    LOG("Q_send -> queue 0x%x size %u (depth %u) notify %08x->task 0x%x\n",id,size,used+1,nbits,owner);
    /* REPLAY_TRIGGER: record startup self-messages to CfgServerQueue (verbatim, valid bytes) */
    if(replay_trigger && !runup_done && msg && size && size<=CFGQ_MSG
       && !strcmp(C->queues[s].name,"CfgServerQueue")){
        int k=__atomic_fetch_add(&cfgq_n,1,__ATOMIC_ACQ_REL);
        if(k<CFGQ_CAP){ cfgq_rec[k].len=size; memcpy(cfgq_rec[k].data,msg,size); }
        else __atomic_store_n(&cfgq_n,CFGQ_CAP,__ATOMIC_RELEASE);
    }
    return 0;
}
static int q_read(uint32_t id,void*buf,uint32_t maxsize,uint32_t timeout,uint32_t*hdrbuf){
    int s=q_slot(id); if(s<0){ LOG("Q_read unknown queue 0x%x\n",id); return -9; }
    if(timeout==0xffffffff && qread_maxwait) timeout=qread_maxwait;   /* debug: cap ALL forever-waits */
    if(timeout==0xffffffff && sync_timeout && strstr(C->queues[s].name,"Sync")){ /* cap only *Sync handshakes */
        /* NB: capping the HwsM* HWS reply mailslots does NOT help — the run-up's SyncMessage
         * re-reads the reply queue after each timeout (polls forever), it does not degrade on a
         * missing HW server. It needs a REAL reply injected (QHWServer stub). See docs/17. */
        timeout=sync_timeout; LOG("Q_read \"%s\" forever-wait capped to %ums (no server peer)\n",C->queues[s].name,sync_timeout); }
    struct queue*q=&C->queues[s];
    for(;;){
        lock();
        if(q->tail!=q->head){
            uint32_t slot=q->head%QSLOTS;
            uint32_t len=q->msg[slot].len; if(maxsize&&len>maxsize) len=maxsize;
            if(buf&&len) memcpy(buf,q->msg[slot].data,len);
            if(hdrbuf){ hdrbuf[0]=q->msg[slot].hdr[0]; hdrbuf[1]=q->msg[slot].hdr[1]; hdrbuf[2]=q->msg[slot].hdr[2]; }
            qhex("Q_read",id,buf,len);
            __atomic_add_fetch(&q->head,1,__ATOMIC_ACQ_REL);
            unlock();
            LOG("Q_read <- queue 0x%x size %u\n",id,len);
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
        }
        uint32_t t=q->tail; unlock();
        if(timeout==0) return -0x35;                      /* empty, no-wait */
        struct timespec ts,*tp=0;
        if(timeout!=0xffffffff){ ts.tv_sec=timeout/1000; ts.tv_nsec=(timeout%1000)*1000000L; tp=&ts; }
        futex(&q->tail,FUTEX_WAIT,t,tp);
        if(timeout!=0xffffffff && q->tail==q->head) return -0x35;   /* timed out */
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
    /* INJECT_REREAD: now that ConfigServer's run-up is done, post a synthetic UpdNewState onto
     * CfgServerQueue. OnUpdNewState → CfgServer::ReadConfigDataSet → ReadDataFiles LOADS the config
     * DATA files (tnc.cfg → "NC" channel group). (CfgRereadData/OnRereadData is write-back/refresh,
     * NOT the initial load — ReadDataFiles' callers are ReadConfigDataSet, reached via OnUpdNewState.)
     * Posting at run-up loads the config BEFORE IPO connects+queries. Needs /etc/jhvolume colon-form
     * so the "SYS:\config\..." paths resolve. UpdNewState wire = the proven v2 schema (id 0x1f0320). */
    if(inject_reread && !reread_injected){
        int cs=q_find_slot("CfgServerQueue");
        if(cs>=0){
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
    }
}
/* INJECT_ACK: IPO sent CfgConnectClient(0x1700c0). Parse its reply-queue name (first GMsgString)
 * and post a synthetic CfgClientIsConnected(0x170100, success=OK) to it so IPO proceeds. */
static void put32(unsigned char*b,uint32_t v){ b[0]=v; b[1]=v>>8; b[2]=v>>16; b[3]=v>>24; }
static int ack_injected=0;
static void inject_connect_ack(uint32_t target_qid,const void*msg,uint32_t size){
    if(!inject_ack||ack_injected||!msg||size<16) return;
    int ts=q_slot(target_qid); if(ts<0||strcmp(C->queues[ts].name,"CfgServerQueue")) return;
    const unsigned char*m=msg;
    uint32_t hdr=m[0]|(m[1]<<8)|(m[2]<<16)|((uint32_t)m[3]<<24);
    if((hdr&0x7fffffff)!=0x1700c0) return;               /* not a CfgConnectClient */
    uint32_t tag=m[4]|(m[5]<<8)|(m[6]<<16)|((uint32_t)m[7]<<24);
    if(tag!=0xe7) return;                                 /* expect GMsgString reply-queue name */
    uint32_t nlen=m[8]|(m[9]<<8)|(m[10]<<16)|((uint32_t)m[11]<<24);
    if(nlen==0||nlen>64||12+nlen>size) return;
    char name[80]; memcpy(name,m+12,nlen); name[nlen]=0;
    int rs=q_find_slot(name); if(rs<0){ LOG("INJECT_ACK: reply queue \"%s\" not found\n",name); return; }
    uint32_t rqid=C->queues[rs].id;
    /* CfgClientIsConnected: header + clientId(name) + id(marker) + success(enum=OK 0) = 3 fields */
    unsigned char ack[160]; uint32_t p=0;
    put32(ack+p,0x00170100); p+=4;                        /* header (CfgClientIsConnected id) */
    put32(ack+p,0x000000e7); p+=4;                        /* field0 clientId: GMsgString tag (present) */
    put32(ack+p,nlen); p+=4; memcpy(ack+p,name,nlen); p+=nlen;
    put32(ack+p,0x80000063); p+=4;                        /* field1 id: marker (absent/default) */
    put32(ack+p,0x001700eb); p+=4;                        /* field2 success: enum tag (present) */
    put32(ack+p,0x00000000); p+=4;                        /* success value = OK(0) */
    ack_injected=1;
    q_send(rqid,ack,p,0);
    LOG("INJECT_ACK: posted CfgClientIsConnected(success=OK) to \"%s\" (0x%x), %u bytes\n",name,rqid,p);
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
        const char *ad=getenv("HEROSCALL_AS_DELIVER"); as_deliver=!(ad&&ad[0]=='0');
        const char *aq=getenv("HEROSCALL_AUTO_QUEUE"); q_autocreate=!(aq&&aq[0]=='0');
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
        const char *ir=getenv("HEROSCALL_INJECT_REREAD"); inject_reread=ir&&ir[0]=='1';
        ctl_init();
        __atomic_store_n(&g_inited,1,__ATOMIC_RELEASE);
    }
    __atomic_store_n(&g_guard,0,__ATOMIC_RELEASE);
}
__attribute__((constructor)) static void rtos_init(void){ ensure_init(); }

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
        if(p[12]) ev_send(p[12],0x80000);              /* wake t_create_ex's ev_receive(0x80000) */
        LOG("T_create task 0x%x parent 0x%x ctxbuf %08x -> wait t_start\n",T,p[12],p[10]);
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
        return r;
    }
    case 0x0e: /* Q_read(outbuf@p[0], maxsize@p[2], hdr/base@p[4], timeout@p[6], qid@p[7]).
                * p[4]!=0 (q_receive) -> kernel writes a 12-byte sender header there. */
        if(p) return q_read(p[7], (void*)(uintptr_t)p[0], p[2], p[6], (uint32_t*)(uintptr_t)p[4]);
        return -9;
    case 0x1a:{ /* Tm_wkafter(ms@p[0]) — sleep, then return 0 */
        if(p&&p[0]){ struct timespec ts={p[0]/1000,(long)(p[0]%1000)*1000000L};
            raw5(SYS_nanosleep,(long)&ts,0,0,0,0); }
        return 0;
    }
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
    default:
        return 0;   /* T_create,T_start,As,P,Tm etc — success stub for now */
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
