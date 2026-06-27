/* skconnforce.c — diagnostic LD_PRELOAD for Guppy.elf (gated SKCONNFORCE, default ON when loaded; load FIRST).
 *
 * Forces the softkey CONNECTION past the cold-VM GData connect stall so Guppy actually transmits SkMgrLogin
 * to skmgr (and the real protocol — InfoRequests / SetMenu — then flows over the HeROS queues, which DO work
 * under FEX). The blocker (RE'd, libSkMgrCtrl.so):
 *   SkMgrCtrlInterfaceImpl::SendMessage@0xc080:
 *     c097: 80 7e 30 00   cmpb $0,0x30(%esi)   ; conn48 (this+0x30 = the connection-established flag)
 *     c09b: 74 6b         je   0xc108          ; conn48==0 -> return 3 (NOT CONNECTED, message NOT sent)
 *   Under per-process FEX the cross-process GData connect handshake never completes, so conn48 stays 0,
 *   so EVERY SkMgrLogin send returns 3 and skmgr gets 0 messages -> no client -> no menu -> no softkey bar.
 *   GUPPYSKMGR_::Connect@0xbc9a0 then spins its a2=-1 retry-recursion forever (never "connected").
 * FIX: NOP the je (74 6b -> 90 90) so SendMessage always proceeds to actually q_send the message. skmgr then
 *   runs OnLogin -> replies SkMgrLoginQuit (SK_REPLY_FORCE routes it to Guppy's notify queue) ->
 *   WaitForExpectedMessage receives it -> conn48 / connection-id get set from the reply -> the recursion ends
 *   and Register drives the rest of the real client protocol (SetMenu etc.) -> skmgr loads the 19 .bmx.
 * (Forced trigger in the cfgfix/skforce style; the faithful alternative is a working GData cross-process
 *  connection bridge, the documented multi-process frontier.)
 * Optional SKCONNFORCE_CHECKSTATE=1 ALSO forces SendMessage past the CheckState gate (c0b0 je 0xc118 -> NOP)
 * in case the not-yet-connected state also fails CheckState (returns 5).
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o skconnforce.so emulator/skconnforce.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

static uintptr_t lib_base(const char*soname){
    FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
    char line[1024]; uintptr_t lo=0;
    while(fgets(line,sizeof line,f)){
        if(strstr(line,soname)){
            uintptr_t s=strtoul(line,0,16);
            if(s && (lo==0 || s<lo)) lo=s;      /* lowest mapping of the lib = load base */
        }
    }
    fclose(f); return lo;
}

static int patch(uintptr_t addr, const unsigned char*expect, const unsigned char*repl, int n, const char*what){
    unsigned char*p=(unsigned char*)addr;
    long pg=sysconf(_SC_PAGESIZE);
    void*pa=(void*)((uintptr_t)p & ~(uintptr_t)(pg-1));
    if(mprotect(pa,(size_t)pg*2,PROT_READ|PROT_WRITE|PROT_EXEC)!=0){
        fprintf(stderr,"[skconnforce] mprotect failed @%p (%s)\n",p,what); return 0; }
    for(int i=0;i<n;i++) if(p[i]!=expect[i]){
        fprintf(stderr,"[skconnforce] %s: unexpected bytes @%p (have %02x want %02x) — NOT patching\n",what,p+i,p[i],expect[i]);
        mprotect(pa,(size_t)pg*2,PROT_READ|PROT_EXEC); return 0; }
    for(int i=0;i<n;i++) p[i]=repl[i];
    mprotect(pa,(size_t)pg*2,PROT_READ|PROT_EXEC);
    fprintf(stderr,"[skconnforce] patched %s @%p\n",what,p);
    return 1;
}

__attribute__((constructor))
static void skconnforce_init(void){
    const char*e=getenv("SKCONNFORCE");
    if(e && e[0]=='0'){ fprintf(stderr,"[skconnforce] SKCONNFORCE=0 -> not patching\n"); return; }
    uintptr_t base=lib_base("libSkMgrCtrl.so");
    if(!base){ return; }   /* not in a process that maps libSkMgrCtrl (e.g. skmgr/ConfigServer) — silent */
    fprintf(stderr,"[skconnforce] libSkMgrCtrl.so base=%lx\n",(unsigned long)base);
    /* conn48 gate: c09b 74 6b (je return3) -> 90 90 (fall through, always send) */
    { unsigned char ex[2]={0x74,0x6b}, rp[2]={0x90,0x90};
      patch(base+0xc09b, ex, rp, 2, "SendMessage conn48 je->nop"); }
    const char*cs=getenv("SKCONNFORCE_CHECKSTATE");
    if(cs && cs[0]=='1'){
        /* CheckState gate: c0b0 74 66 (je return5) -> 90 90 */
        unsigned char ex[2]={0x74,0x66}, rp[2]={0x90,0x90};
        patch(base+0xc0b0, ex, rp, 2, "SendMessage CheckState je->nop");
    }
}
