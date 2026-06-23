/* abortbt.c — i386 LD_PRELOAD: print a guest backtrace when glibc aborts.
 * glibc's malloc consistency checks ("free(): invalid pointer", "double free", ...) end in
 * malloc_printerr -> __libc_message -> abort(). We interpose abort() (and __assert_fail) so the
 * GUEST call stack is dumped before the process dies — ConfigServer.elf is NOT stripped, so
 * backtrace_symbols_fd yields function names, pinning exactly which code frees the bad pointer.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o abortbt.so emulator/abortbt.c -ldl
 * Preload it FIRST so the interposer wins. */
#define _GNU_SOURCE
#include <execinfo.h>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <unistd.h>

static void dump_bt(const char *why){
    void *bt[96];
    int n = backtrace(bt, 96);
    fprintf(stderr, "\n[abortbt] ===== %s — guest backtrace (%d frames) =====\n", why, n);
    fflush(stderr);
    backtrace_symbols_fd(bt, n, 2);
    fprintf(stderr, "[abortbt] ===== end backtrace =====\n");
    fflush(stderr);
}

void abort(void){
    dump_bt("abort()");
    static void (*real)(void) = NULL;
    if(!real) real = dlsym(RTLD_NEXT, "abort");
    if(real) real();
    _exit(134);
}

void __assert_fail(const char *a, const char *f, unsigned l, const char *fn){
    fprintf(stderr, "[abortbt] assert: %s @ %s:%u (%s)\n", a, f, l, fn);
    dump_bt("__assert_fail");
    static void (*real)(const char*,const char*,unsigned,const char*) = NULL;
    if(!real) real = dlsym(RTLD_NEXT, "__assert_fail");
    if(real) real(a,f,l,fn);
    _exit(134);
}
