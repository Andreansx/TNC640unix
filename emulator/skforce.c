/* skforce.c — diagnostic LD_PRELOAD for Guppy.elf (gated SKFORCE, default ON when loaded; load FIRST).
 *
 * Forces PyJHCallback::SKRegister@0xb45e0 to ALWAYS proceed to GUPPYSKMGR::Register (= contact skmgr)
 * even when the HwViewer window is not "bind-capable". The bind gate (RE'd):
 *   0xb470c: cmp byte[edi+0x1C], 0   ; window record +0x1C ("WndFullScreen bind-capable" flag)
 *   0xb4710: jnz  loc_B476B          ; +0x1C != 0 -> GUPPYSKMGR::Register (the skmgr path)   <-- 75 59
 *   0xb4712: cmp byte[edi+0x14], 0   ; window record +0x14 (WndPlcScreen/FocusPane flag)
 *   0xb4716: jnz  loc_B47E8          ; +0x14 != 0 -> GUPPYSKMGR::Register (other arm)
 *   0xb471c: Err_Set(ER_SOE_SK_BIND_WINDOW)  ; both 0 -> "Binding softkey resource to window failed"
 * Without a window manager (winmgr) the OEM window never becomes bind-capable (+0x1C/+0x14 stay 0),
 * so SKRegister bails BEFORE ever contacting skmgr. This patch turns the first jnz (75) into jmp (EB)
 * so the register ALWAYS reaches GUPPYSKMGR::Register -> SkMgrCtrl::SendMessage -> q_send(Q_SkMgr) ->
 * skmgr. That lets us exercise + RE the skmgr register/reply protocol (goal step 3) without winmgr.
 * (Diagnostic forcing in the cfgfix/hwforce style; the faithful alternative is to bring up winmgr.elf.)
 * Guppy.elf is a PIE: find its load base in /proc/self/maps; runtime addr = base + vaddr.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o skforce.so emulator/skforce.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/mman.h>

static uintptr_t guppy_base(void){
    FILE*f=fopen("/proc/self/maps","r"); if(!f) return 0;
    char line[1024]; uintptr_t lo=0;
    while(fgets(line,sizeof line,f)){
        if(strstr(line,"Guppy.elf")){
            uintptr_t s=strtoul(line,0,16);
            if(s && (lo==0 || s<lo)) lo=s;       /* lowest Guppy.elf mapping = PIE load base */
        }
    }
    fclose(f); return lo;
}

__attribute__((constructor))
static void skforce_init(void){
    const char*e=getenv("SKFORCE");
    if(e && e[0]=='0'){ fprintf(stderr,"[skforce] SKFORCE=0 -> not patching\n"); return; }
    uintptr_t base=guppy_base();
    if(!base){ fprintf(stderr,"[skforce] Guppy.elf base not found (not in Guppy?)\n"); return; }
    unsigned char*p=(unsigned char*)(base+0xb4710);
    long pg=sysconf(_SC_PAGESIZE);
    void*pa=(void*)((uintptr_t)p & ~(uintptr_t)(pg-1));
    if(mprotect(pa,(size_t)pg*2,PROT_READ|PROT_WRITE|PROT_EXEC)!=0){
        fprintf(stderr,"[skforce] mprotect failed @%p\n",p); return; }
    /* sanity: the bind gate is `cmp byte[edi+0x1C],0` (80 7f 1c 00) then `jnz` (75 59) */
    unsigned char*c=(unsigned char*)(base+0xb470c);
    if(!(c[0]==0x80 && c[1]==0x7f && c[2]==0x1c && c[3]==0x00 && p[0]==0x75)){
        fprintf(stderr,"[skforce] unexpected gate bytes @0xb470c: %02x %02x %02x %02x / jnz=%02x (base=%lx) — NOT patching\n",
                c[0],c[1],c[2],c[3],p[0],(unsigned long)base);
        mprotect(pa,(size_t)pg*2,PROT_READ|PROT_EXEC); return; }
    p[0]=0xEB;   /* jnz loc_B476B -> jmp loc_B476B : always take the GUPPYSKMGR::Register path */
    mprotect(pa,(size_t)pg*2,PROT_READ|PROT_EXEC);
    fprintf(stderr,"[skforce] patched SKRegister bind-gate@%p (base=%lx) jnz->jmp -> always contact skmgr\n",
            p,(unsigned long)base);
}
