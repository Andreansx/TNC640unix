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
}
