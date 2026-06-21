/* heroscall_probe2.c — dump full param struct per heroscall command to learn the
 * RTOS ABI layouts empirically. i386 LD_PRELOAD, raw int 0x80 passthrough. */
#define _GNU_SOURCE
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/syscall.h>

long syscall(long n, ...);
static long raw5(long n,long a,long b,long c,long d,long e){ long r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e):"memory"); return r; }

static const char* nm(int lo){ switch(lo){
  case 0x01:return"T_ident";case 0x02:return"T_start";case 0x09:return"T_name";
  case 0x0a:return"Q_create";case 0x0d:return"Q_send";case 0x0e:return"Q_read";
  case 0x10:return"Ev_send";case 0x11:return"Ev_receive";case 0x15:return"Sm_create";
  case 0x16:return"Sm_ident";case 0x18:return"Sm_request";case 0x1a:return"Tm_wkafter";
  case 0x21:return"M_create";case 0x22:return"M_ident";case 0x23:return"M_attach";
  case 0x24:return"M_detach";case 0x26:return"Sys_setenv";case 0x27:return"Sys_getenv";
  case 0x29:return"P_ident";case 0x2a:return"P_childstat";case 0x2b:return"P_?2b";
  case 0x2d:return"P_?2d";default:return"?";} }

static int looks_ptr(uint32_t v){ return v>0x1000 && v<0xfffff000; }

long syscall(long n,...){
    va_list ap; va_start(ap,n);
    long a=va_arg(ap,long),b=va_arg(ap,long),c=va_arg(ap,long),d=va_arg(ap,long),e=va_arg(ap,long);
    va_end(ap);
    if(n==222){
        uint32_t cmd=(uint32_t)a; int lo=cmd&0xff; uint32_t *p=(uint32_t*)b;
        fprintf(stderr,"[hc %02x %-10s] p=", lo, nm(lo));
        if(p){ for(int i=0;i<8;i++) fprintf(stderr,"%08x ",p[i]); } else fprintf(stderr,"(null)");
        fprintf(stderr,"a2=%lx\n",c);
        /* deref ONLY p[0] as a name string, ONLY for name-arg commands (getenv/M_ident/Sm_ident) */
        if(p && (lo==0x27||lo==0x22||lo==0x16) && looks_ptr(p[0])){
            char *s=(char*)(uintptr_t)p[0];
            fprintf(stderr,"     name=\"%.31s\"\n",s);
        }
        return 0;
    }
    return raw5(n,a,b,c,d,e);
}
