/*
 * heroscall_probe.c — LD_PRELOAD probe (i386). Interposes libc syscall() to
 * observe the HeROS heroscall (222) traffic during control init, and decodes the
 * two commands init issues:
 *   0x12340001 T_ident      — task lookup by name
 *   0x12340027 Sys_getenv   — read a HeROS environment variable
 * The shim runs in-process, so the i386 arg pointer is directly dereferenceable.
 * Goal: capture WHICH task names / env vars the PciHardware probe asks for.
 *
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -fno-stack-protector heroscall_probe.c -o heroscall_probe.so
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/syscall.h>

long syscall(long n, ...);   /* our own interposer, used below too */

static int fake_fds[64], fake_n;
static int is_heros_dev(const char *p){ return p && (strstr(p,"herosapi")||strstr(p,"/dev/events")); }
static int is_fake(int fd){ for(int i=0;i<fake_n;i++) if(fake_fds[i]==fd) return 1; return 0; }
static int make_fake(const char *p){ int fd=(int)syscall(SYS_openat,AT_FDCWD,"/dev/zero",O_RDWR,0);
    if(fd>=0&&fake_n<64) fake_fds[fake_n++]=fd; return fd; }
int open(const char *p,int fl,...){ if(is_heros_dev(p)) return make_fake(p);
    va_list a; va_start(a,fl); int m=va_arg(a,int); va_end(a); return (int)syscall(SYS_openat,AT_FDCWD,p,fl,m); }
int open64(const char *p,int fl,...){ if(is_heros_dev(p)) return make_fake(p);
    va_list a; va_start(a,fl); int m=va_arg(a,int); va_end(a); return (int)syscall(SYS_openat,AT_FDCWD,p,fl|O_LARGEFILE,m); }
int openat(int df,const char *p,int fl,...){ if(is_heros_dev(p)) return make_fake(p);
    va_list a; va_start(a,fl); int m=va_arg(a,int); va_end(a); return (int)syscall(SYS_openat,df,p,fl,m); }
int ioctl(int fd,unsigned long req,...){ va_list a; va_start(a,req); void*arg=va_arg(a,void*); va_end(a);
    if(is_fake(fd)) return 0; return (int)syscall(SYS_ioctl,fd,req,arg); }

static long raw5(long n,long a,long b,long c,long d,long e){ long r;
    __asm__ volatile("int $0x80":"=a"(r):"a"(n),"b"(a),"c"(b),"d"(c),"S"(d),"D"(e):"memory"); return r; }

/* dump up to 16 bytes of a (possibly non-string) name as hex + ascii */
static void hexname(const unsigned char *p){
    if(!p){ fprintf(stderr,"(null)"); return; }
    fprintf(stderr,"\"");
    for(int i=0;i<8;i++){ unsigned char c=p[i]; fputc((c>=0x20&&c<0x7f)?c:'.',stderr); }
    fprintf(stderr,"\" [%02x %02x %02x %02x %02x %02x %02x %02x]",p[0],p[1],p[2],p[3],p[4],p[5],p[6],p[7]);
}

long syscall(long n,...){
    va_list ap; va_start(ap,n);
    long a=va_arg(ap,long),b=va_arg(ap,long),c=va_arg(ap,long),d=va_arg(ap,long),e=va_arg(ap,long);
    va_end(ap);
    if(n==222){
        uint32_t cmd=(uint32_t)a; uint32_t *param=(uint32_t*)b;   /* arg ptr (i386, in-process) */
        if(cmd==0x12340027 && param){                            /* Sys_getenv: param[0]=char* name */
            const char *name=(const char*)(uintptr_t)param[0];
            fprintf(stderr,"[probe] Sys_getenv name=\"%s\"\n", name?name:"(null)");
        } else if(cmd==0x12340001 && param){                     /* T_ident: name in first 8 bytes */
            fprintf(stderr,"[probe] T_ident name="); hexname((const unsigned char*)param); fprintf(stderr,"\n");
        } else {
            fprintf(stderr,"[probe] heroscall cmd=0x%08x param=%p arg2=0x%lx\n",cmd,(void*)param,c);
        }
        return 0;                                                /* still a stub */
    }
    return raw5(n,a,b,c,d,e);
}
