/* hwforce.c — diagnostic LD_PRELOAD for HrMmi.elf (gated; load FIRST).
 * Patches HrModule's static HandwheelUsesHrMmi(GMsgArray<HR_TYPE>)@vaddr 0x298f0 to `mov eax,1; ret`
 * so it ALWAYS reports the handwheel uses HrMmi. Then OnHrMmiCfgGlobal's `target = 1 + HandwheelUsesHrMmi`
 * (write @0x3711d) and OnCfgActiveHandwheel both set the active-state target to 2 (=active) — IF they reach
 * that point. Experiment: if a window appears, the target write IS reached and the only thing missing was
 * the handwheel=true value (target was 1=asleep or 0); if not, OnHrMmiCfgGlobal bails earlier (config
 * incomplete). HrMmi.elf is a PIE: find its load base in /proc/self/maps; runtime addr = base + vaddr. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

static uintptr_t hrmmi_base(void){
    FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
    char line[1024]; uintptr_t lo=0;
    while(fgets(line,sizeof line,f)){
        if(strstr(line,"HrMmi.elf")){
            uintptr_t s=strtoul(line,0,16);
            if(s && (lo==0 || s<lo)) lo=s;       /* lowest HrMmi.elf mapping = PIE load base */
        }
    }
    fclose(f); return lo;
}

__attribute__((constructor))
static void hwforce_init(void){
    uintptr_t base=hrmmi_base();
    if(!base){ fprintf(stderr,"[hwforce] HrMmi.elf base not found (not in HrMmi?)\n"); return; }
    unsigned char*p=(unsigned char*)(base+0x298f0);
    long pg=sysconf(_SC_PAGESIZE);
    void*pa=(void*)((uintptr_t)p & ~(uintptr_t)(pg-1));
    if(mprotect(pa,(size_t)pg*2,PROT_READ|PROT_WRITE|PROT_EXEC)!=0){
        fprintf(stderr,"[hwforce] mprotect failed @%p\n",p); return; }
    /* sanity: HandwheelUsesHrMmi starts `push ebp; mov ebp,eax` = 55 89 C5 */
    if(!(p[0]==0x55 && p[1]==0x89 && p[2]==0xC5)){
        fprintf(stderr,"[hwforce] unexpected prologue @0x298f0: %02x %02x %02x (base=%lx) — NOT patching\n",
                p[0],p[1],p[2],(unsigned long)base);
        mprotect(pa,(size_t)pg*2,PROT_READ|PROT_EXEC); return; }
    /* mov eax,1 ; ret */
    p[0]=0xB8; p[1]=0x01; p[2]=0x00; p[3]=0x00; p[4]=0x00; p[5]=0xC3;
    mprotect(pa,(size_t)pg*2,PROT_READ|PROT_EXEC);
    fprintf(stderr,"[hwforce] patched HandwheelUsesHrMmi@%p (base=%lx) -> return 1 (force active-state target=2)\n",
            p,(unsigned long)base);

    /* PATCH 2 (the render lever): OnCfgActiveHandwheel@0x37580 gates its render path on GMessage::IsValid(msg,0).
     * The CfgActiveHandwheel ConfigServer serves in the demo prog-station is MINIMAL (mostly-absent fields) so
     * (a) it FAILS IsValid -> the failure branch sets target=0, renders nothing; and (b) even forced past IsValid,
     * the success path at loc_376D0 calls HrMailer::Configure(CfgActiveHandwheel)@0x376dc FIRST, which THROWS on
     * the invalid message -> unwinds before the target-set + Move (the real control gets a complete handwheel
     * config; this demo image has none). FIX: patch the IsValid branch `jnz loc_376D0` (0x375b3: 0f 85 17 01 00
     * 00) to `jmp loc_37740; nop` (e9 88 01 00 00 90) — loc_37740 is the (target<=1) target-set arm: it does the
     * GMsgArray<HR_TYPE> copy from body+72 (empty array -> safe) + HandwheelUsesHrMmi (patched->1) -> sets the
     * active-state target=2, then MoveActiveStateTowardsTarget -> Activate -> UpdateDisplay = the first window.
     * This SKIPS both the IsValid check AND the throwing HrMailer::Configure. rel32 = 0x37740-(0x375b3+5)=0x188.
     * Gated FORCE_AHW_VALID (default ON when hwforce loads); set FORCE_AHW_VALID=0 to A/B. */
    const char*fav=getenv("FORCE_AHW_VALID");
    if(!fav || fav[0]!='0'){
        unsigned char*q=(unsigned char*)(base+0x375b3);
        void*qa=(void*)((uintptr_t)q & ~(uintptr_t)(pg-1));
        if(mprotect(qa,(size_t)pg*2,PROT_READ|PROT_WRITE|PROT_EXEC)==0){
            if(q[0]==0x0f && q[1]==0x85 && q[2]==0x17 && q[3]==0x01){     /* jnz loc_376D0 rel32=0x117 */
                /* jump straight to 0x37782 = `mov [edi+0E4h],2 (target=2); jmp loc_376B6 (Move)` — skips
                 * IsValid + HrMailer::Configure + the GMsgArray<HR_TYPE> copy from body+72 (which would
                 * throw on the minimal/invalid demo message). rel32 = 0x37782-(0x375b3+5) = 0x1CA. */
                q[0]=0xe9; q[1]=0xca; q[2]=0x01; q[3]=0x00; q[4]=0x00; q[5]=0x90;  /* jmp 0x37782; nop */
                mprotect(qa,(size_t)pg*2,PROT_READ|PROT_EXEC);
                fprintf(stderr,"[hwforce] patched OnCfgActiveHandwheel IsValid-branch@%p -> jmp 0x37782 (target=2 + Move, skip all msg-dependent code)\n",q);
            } else {
                fprintf(stderr,"[hwforce] unexpected bytes @0x375b3: %02x %02x %02x %02x — NOT patching IsValid branch\n",q[0],q[1],q[2],q[3]);
                mprotect(qa,(size_t)pg*2,PROT_READ|PROT_EXEC);
            }
        }
    }

    /* PATCH 3 (FORCE_MOVE, default ON when hwforce loads; =0 to A/B): MoveActiveStateTowardsTarget@0x33a60
     * bails on the startup request-counter (HrModule+59 / +0xEC != 0). With OnHrMmiCfgGlobal bailing on the
     * incomplete demo config, the request accounting never settles to exactly 0, so Move never advances
     * current 0 -> target -> Activate -> HRDATAIF::UpdateDisplay = the window. There are 4 counter checks
     * (mov reg,[eax+0xEC]; test; jnz loc_3443F) at 0x33a82 / 0x33f9a / 0x3440d / 0x3447e — NOP the jnz at each
     * so Move advances regardless of the counter. (Diagnostic/forcing: confirms the render path; the faithful
     * fix is to drain the counter to 0 by answering every counted request.) */
    const char*fmv=getenv("FORCE_MOVE");
    if(!fmv || fmv[0]!='0'){
        struct { uintptr_t off; int len; unsigned char b0,b1; } chk[]={
            {0x33a82,6,0x0f,0x85}, {0x33f9a,6,0x0f,0x85}, {0x3440d,2,0x75,0x30}, {0x3447e,2,0x75,0xbf} };
        for(unsigned k=0;k<sizeof chk/sizeof chk[0];k++){
            unsigned char*r=(unsigned char*)(base+chk[k].off);
            void*ra=(void*)((uintptr_t)r & ~(uintptr_t)(pg-1));
            if(mprotect(ra,(size_t)pg*2,PROT_READ|PROT_WRITE|PROT_EXEC)!=0) continue;
            if(r[0]==chk[k].b0 && r[1]==chk[k].b1){
                for(int i=0;i<chk[k].len;i++) r[i]=0x90;        /* NOP the counter-bail jnz */
                fprintf(stderr,"[hwforce] patched Move counter-check@%p (+%#x) -> NOP (force advance)\n",r,(unsigned)chk[k].off);
            } else fprintf(stderr,"[hwforce] unexpected bytes @%#x: %02x %02x — NOT patching\n",(unsigned)chk[k].off,r[0],r[1]);
            mprotect(ra,(size_t)pg*2,PROT_READ|PROT_EXEC);
        }
    }
}
