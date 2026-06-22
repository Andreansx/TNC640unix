// openlog.c — tiny LD_PRELOAD diagnostic: log every file open whose path matches a keyword
// (keymap/charmap/functionkey/resource/.xml), with the resulting fd/errno. Built i386 with
// i686-linux-gnu-gcc and loaded into the FEX guest so we see the EXACT path the control opens
// for the PLIB++ keymap/charmap/functionkeymap (and whether it's ENOENT or found-but-parse-fail).
// Covers the fortified __open_2/__open64_2 variants (same lesson as herosapi_shim/fexunmask).
#define _GNU_SOURCE
#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

static int  (*r_open)(const char*, int, ...);
static int  (*r_open64)(const char*, int, ...);
static int  (*r_openat)(int, const char*, int, ...);
static int  (*r___open_2)(const char*, int);
static int  (*r___open64_2)(const char*, int);
static FILE*(*r_fopen)(const char*, const char*);
static FILE*(*r_fopen64)(const char*, const char*);
static int  (*r_access)(const char*, int);

static int match(const char* p){
    return p && (strstr(p,"keymap")||strstr(p,"charmap")||strstr(p,"functionkey")
              || strstr(p,"resource")||strstr(p,".xml")||strstr(p,".ini"));
}
static void logp(const char* fn, const char* p, long ret){
    if(!match(p)) return;
    int e = errno;
    fprintf(stderr, "[openlog] %s(\"%s\") -> %ld%s\n", fn, p, ret,
            ret<0 ? (e==ENOENT?" ENOENT":e==EACCES?" EACCES":e==ENOTDIR?" ENOTDIR":" ERR") : " OK");
    fflush(stderr);
    errno = e;
}

__attribute__((constructor)) static void init(void){
    r_open       = dlsym(RTLD_NEXT,"open");
    r_open64     = dlsym(RTLD_NEXT,"open64");
    r_openat     = dlsym(RTLD_NEXT,"openat");
    r___open_2   = dlsym(RTLD_NEXT,"__open_2");
    r___open64_2 = dlsym(RTLD_NEXT,"__open64_2");
    r_fopen      = dlsym(RTLD_NEXT,"fopen");
    r_fopen64    = dlsym(RTLD_NEXT,"fopen64");
    r_access     = dlsym(RTLD_NEXT,"access");
}

int open(const char* p, int fl, ...){
    mode_t m=0; if(fl&O_CREAT){ va_list a; va_start(a,fl); m=va_arg(a,int); va_end(a);}
    int rc=r_open(p,fl,m); logp("open",p,rc); return rc;
}
int open64(const char* p, int fl, ...){
    mode_t m=0; if(fl&O_CREAT){ va_list a; va_start(a,fl); m=va_arg(a,int); va_end(a);}
    int rc=r_open64(p,fl,m); logp("open64",p,rc); return rc;
}
int openat(int d, const char* p, int fl, ...){
    mode_t m=0; if(fl&O_CREAT){ va_list a; va_start(a,fl); m=va_arg(a,int); va_end(a);}
    int rc=r_openat(d,p,fl,m); logp("openat",p,rc); return rc;
}
int __open_2(const char* p, int fl){ int rc=r___open_2(p,fl); logp("__open_2",p,rc); return rc; }
int __open64_2(const char* p, int fl){ int rc=r___open64_2(p,fl); logp("__open64_2",p,rc); return rc; }
FILE* fopen(const char* p, const char* mo){ FILE* f=r_fopen(p,mo); logp("fopen",p,f?0:-1); return f; }
FILE* fopen64(const char* p, const char* mo){ FILE* f=r_fopen64(p,mo); logp("fopen64",p,f?0:-1); return f; }
int access(const char* p, int md){ int rc=r_access(p,md); logp("access",p,rc); return rc; }
