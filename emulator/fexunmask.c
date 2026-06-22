/*
 * fexunmask.c — LD_PRELOAD interpose for heuserver (and any HeROS server that authorizes a
 * client by its executable path). Under FEX, a connecting i386 client's /proc/PID/exe resolves
 * to the TRANSLATOR "/usr/bin/FEXInterpreter", not the real heros binary — so heuserver's
 * exe-path privilege table (FUN_00019b70: readlink /proc/PID/exe -> fnmatch pattern table ->
 * priv bits) matches nothing and DENIES every FEX-run client.
 *
 * This shim interposes readlink()/readlinkat(): for "/proc/<pid>/exe" whose real target is the
 * FEX interpreter, it returns the REAL program path instead — taken from /proc/<pid>/cmdline,
 * which FEX runs as `FEXInterpreter <real-binary> [args...]`, so cmdline argv[1] is the binary.
 * Non-FEX exe links and all other readlinks pass through unchanged.
 *
 * Uses libc syscall() (like herosapi_shim) so it does not pull a newer glibc symbol version than
 * the control's glibc 2.31. Debug: HEU_UNMASK_DBG=1 -> stderr trace.
 * Build (i386, in VM): i686-linux-gnu-gcc -shared -fPIC -O2 -o fexunmask.so fexunmask.c
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>

long syscall(long n, ...);

static int dbg = -1;
static void dbginit(void){ if(dbg<0){ const char*e=getenv("HEU_UNMASK_DBG"); dbg=(e&&e[0]=='1'); } }

__attribute__((constructor)) static void fxinit(void){
    const char m[]="[fexunmask] loaded\n"; syscall(SYS_write,2L,(long)m,(long)sizeof(m)-1);
}

static int is_proc_exe(const char*p){
    if(!p) return 0;
    if(strncmp(p,"/proc/",6)) return 0;
    size_t n=strlen(p);
    return n>4 && strcmp(p+n-4,"/exe")==0;
}

/* fill buf with the FEX-unmasked path if applicable; return new length, or -1 to fall through */
static long unmask(const char*path,char*buf,size_t len){
    if(!is_proc_exe(path)) return -1;
    dbginit();
    /* read /proc/<pid>/cmdline: NUL-separated argv. Under FEX this is either
     *   "FEXInterpreter\0<real-binary>\0..."  (translator prefix preserved), or
     *   "<real-binary>\0..."                   (FEX rewrote argv[0] to the guest). */
    char cmd[64], cl[2048];
    snprintf(cmd,sizeof(cmd),"%.*s/cmdline",(int)(strlen(path)-4),path);
    int fd=(int)syscall(SYS_open,(long)cmd,(long)O_RDONLY,0L);
    if(fd<0) return -1;
    long cn=syscall(SYS_read,(long)fd,(long)cl,(long)sizeof(cl)-1);
    syscall(SYS_close,(long)fd);
    if(cn<=0) return -1;
    cl[cn]=0;
    const char*a0=cl;
    int o=0; while(o<cn && cl[o]) o++; o++;              /* index of argv[1] */
    const char*a1=(o<cn)?cl+o:"";
    if(dbg)fprintf(stderr,"[fexunmask] %s cmdline argv0=\"%s\" argv1=\"%s\"\n",path,a0,a1);
    const char*real=0;
    if(strstr(a0,"FEXInterpreter")){ if(a1[0]) real=a1; }   /* prefix form: real = argv[1] */
    else if(a0[0]=='/') real=a0;                            /* rewritten form: real = argv[0] */
    if(!real||!real[0]) return -1;
    size_t rl=strlen(real); if(rl>len) rl=len;
    memcpy(buf,real,rl);
    if(dbg)fprintf(stderr,"[fexunmask] %s : FEX -> real \"%.*s\"\n",path,(int)rl,real);
    return (long)rl;
}

ssize_t readlink(const char*path,char*buf,size_t len){
    dbginit(); if(dbg&&path&&!strncmp(path,"/proc/",6)) fprintf(stderr,"[fexunmask] readlink(%s)\n",path);
    long r=unmask(path,buf,len);
    if(r>=0) return (ssize_t)r;
    return (ssize_t)syscall(SYS_readlinkat,(long)AT_FDCWD,(long)path,(long)buf,(long)len);
}
ssize_t readlinkat(int dfd,const char*path,char*buf,size_t len){
    if(dfd==AT_FDCWD || (path&&path[0]=='/')){
        long r=unmask(path,buf,len);
        if(r>=0) return (ssize_t)r;
    }
    return (ssize_t)syscall(SYS_readlinkat,(long)dfd,(long)path,(long)buf,(long)len);
}
