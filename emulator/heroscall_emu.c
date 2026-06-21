/* heroscall_emu.c — userspace emulator for the HeROS pSOS-style RTOS gateway
 * (i386 syscall 222). LD_PRELOAD into the i386 control; interpose libc syscall(),
 * dispatch cmd=0x1234_00NN, emulate the RTOS primitives, pass everything else to
 * the real kernel via raw int 0x80. ABI recovered from heros.ko (heros_ko.decomp.c)
 * + live param-struct dumps.
 *
 * v1 goal: get the NCK past PciHardware::Exception by serving the IPO_SHARED_MEMORY
 * region (M_ident -> region id, M_attach -> real zeroed mapping). */
#define _GNU_SOURCE
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/syscall.h>

long syscall(long n, ...);
static long raw5(long n,long a,long b,long c,long d,long e){ long r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e):"memory"); return r; }

static int dbg=1;
#define LOG(...) do{ if(dbg) fprintf(stderr,"[emu] " __VA_ARGS__); }while(0)

static const char* hcname(int lo){ switch(lo){
  case 0x01:return"T_ident";case 0x02:return"T_start";case 0x03:return"T_delete";
  case 0x07:return"T_getargs";case 0x08:return"T_setname";case 0x09:return"T_name";
  case 0x0a:return"Q_create";case 0x0b:return"Q_ident";case 0x0d:return"Q_send";
  case 0x0e:return"Q_read";case 0x0f:return"Q_inspect";case 0x10:return"Ev_send";
  case 0x11:return"Ev_receive";case 0x15:return"Sm_create";case 0x16:return"Sm_ident";
  case 0x18:return"Sm_request";case 0x19:return"Sm_release";case 0x1a:return"Tm_wkafter";
  case 0x21:return"M_create";case 0x22:return"M_ident";case 0x23:return"M_attach";
  case 0x24:return"M_detach";case 0x26:return"Sys_setenv";case 0x27:return"Sys_getenv";
  case 0x29:return"P_ident";case 0x2a:return"P_childstat";case 0x2b:return"P_signal";
  case 0x2c:return"P_setname";case 0x2d:return"P_name";case 0x32:return"Sys_control";
  case 0x33:return"Q_name";default:return"?";} }
static int vrb=0;  /* HEROSCALL_VERBOSE=1 → log every command + first 4 dwords */

/* ---------- named memory regions (M_*) ---------- */
#define MAXREG 64
#define REGION_BYTES (64u*1024u*1024u)   /* generous; zeroed; grows by name if needed */
static struct region { char name[20]; uint32_t id; void *addr; size_t size; } regs[MAXREG];
static int nreg=0;
static uint32_t next_rid=0x4000;

static struct region* reg_find_name(const char*n){
    for(int i=0;i<nreg;i++) if(!strcmp(regs[i].name,n)) return &regs[i];
    return 0;
}
static struct region* reg_find_id(uint32_t id){
    for(int i=0;i<nreg;i++) if(regs[i].id==id) return &regs[i];
    return 0;
}
/* M_ident: look up (or lazily create) a region by name, return its id */
static uint32_t reg_ident(const char*name){
    struct region*r=reg_find_name(name);
    if(r){ LOG("M_ident \"%s\" -> id 0x%x (existing)\n",name,r->id); return r->id; }
    if(nreg>=MAXREG){ LOG("M_ident \"%s\" -> region table full\n",name); return (uint32_t)-2; }
    r=&regs[nreg++];
    strncpy(r->name,name,sizeof r->name-1);
    r->id=next_rid++; r->addr=0; r->size=REGION_BYTES;
    LOG("M_ident \"%s\" -> id 0x%x (new)\n",name,r->id);
    return r->id;
}
/* M_attach: map the region (lazily) and return its address */
static void* reg_attach(uint32_t id){
    struct region*r=reg_find_id(id);
    if(!r){ LOG("M_attach id 0x%x -> UNKNOWN region\n",id); return 0; }
    if(!r->addr){
        r->addr=mmap(0,r->size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(r->addr==MAP_FAILED){ r->addr=0; LOG("M_attach \"%s\" mmap FAILED\n",r->name); return 0; }
    }
    LOG("M_attach \"%s\" id 0x%x -> %p (%zu KB)\n",r->name,id,r->addr,r->size/1024);
    return r->addr;
}

/* ---------- env (Sys_getenv) ---------- */
/* The HeROS kernel globenv mirrors the boot-script environment (application/
 * appproduct: SYS=/mnt/sys, OEM=/mnt/plc, USR=/mnt/tnc, SYS_NAME=SYSTEM:, ...).
 * We populate the *process* environ identically in the run script and just read
 * it back via libc getenv(). */
static int env_get(const char*name,char*out,uint32_t sz){
    const char*v=getenv(name);
    if(!v){ if(out&&sz) out[0]=0; LOG("Sys_getenv \"%s\" -> (unset)\n",name); return 0; }
    size_t L=strlen(v);
    if(L>=sz){ if(sz){ memcpy(out,v,sz-1); out[sz-1]=0; } }   /* truncate, still success */
    else memcpy(out,v,L+1);
    LOG("Sys_getenv \"%s\" -> \"%s\"\n",name,v);
    return 0;
}

/* ---------- generic handle allocator (Sm/Q/Ev/T) ---------- */
static uint32_t next_handle=0x101;

static int grant_ev=0;  /* HEROSCALL_GRANT_EVENTS=1 → coarse Ev_receive grant (experimental) */
__attribute__((constructor)) static void emu_init(void){
    const char *v=getenv("HEROSCALL_VERBOSE");      vrb      = v && v[0]=='1';
    const char *g=getenv("HEROSCALL_GRANT_EVENTS"); grant_ev = g && g[0]=='1';
}

long syscall(long n,...){
    va_list ap; va_start(ap,n);
    long a=va_arg(ap,long),b=va_arg(ap,long),c=va_arg(ap,long),d=va_arg(ap,long),e=va_arg(ap,long);
    va_end(ap);
    if(n!=222) return raw5(n,a,b,c,d,e);

    uint32_t cmd=(uint32_t)a; int lo=cmd&0xff; uint32_t *p=(uint32_t*)b;
    if(vrb) fprintf(stderr,"[hc %02x %-11s] p=[%08x %08x %08x %08x] a2=%lx\n",
        lo,hcname(lo),p?p[0]:0,p?p[1]:0,p?p[2]:0,p?p[3]:0,c);
    switch(lo){
    case 0x01: /* T_ident — return a non-zero self task id */
        return 0x101;
    case 0x22: /* M_ident(name@p[0]) -> region id */
        if(p&&p[0]) return (long)(int32_t)reg_ident((const char*)(uintptr_t)p[0]);
        return -2;
    case 0x23: /* M_attach(region id@p[2]) -> mapped addr */
        if(p) return (long)(intptr_t)reg_attach(p[2]);
        return 0;
    case 0x27: /* Sys_getenv(name@p[0], outbuf@p[2], size@p[4]) */
        if(p&&p[0]) return env_get((const char*)(uintptr_t)p[0],(char*)(uintptr_t)p[2],p[4]);
        return -2;
    case 0x11: /* Ev_receive(bits@p[0],cond@p[1],timeout@p[2]) -> received bits in eax.
                * EXPERIMENTAL (HEROSCALL_GRANT_EVENTS=1): grant the awaited bits so a
                * startup-sync wait doesn't busy-loop. Coarse — it lets a single process
                * past the FProcess startup barrier but is NOT faithful event emulation
                * (it breaks FThread/FEvent context creation, which checks event ids).
                * Real multi-threaded servers need a proper task/event runtime. Default 0. */
        if(grant_ev && p) return (long)(int32_t)p[0];
        return 0;
    case 0x15: /* Sm_create -> sem handle */
    case 0x0a: /* Q_create -> queue handle */
    case 0x21: /* M_create -> region handle */
        return (long)(next_handle++);
    default:
        /* generic success for the rest (Sm_request/release, Q_send/read, Ev_*, T_*, Tm_*, P_*) */
        return 0;
    }
}
