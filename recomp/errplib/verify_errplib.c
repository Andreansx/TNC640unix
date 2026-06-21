/*
 * verify_errplib.c — proves the recompiled ARM64 build of libEp90_Errplib's
 * pure-leaf subset is byte-identical to the proprietary i386 .so.
 *
 * Compiled two ways from the SAME source + SAME extern decls:
 *   (truth)   i686-linux-gnu-gcc verify_errplib.c -lEp90_Errplib_trim   (real i386 .so under qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_errplib.c libEp90_Errplib_partial_rebuilt.c
 * Then diff the two outputs.
 *
 * Return-type contracts (must match the i386 ABI exactly, see rebuilt .c):
 *   - bool predicates  -> only `al` is meaningful (sete/setne, eax not preset)
 *   - ERR_IsWarning/ERR_IsError -> full 32-bit eax (deterministic setbe leak)
 *   - ERRPLIB_GetFacilityID     -> full 32-bit eax (table value, eax<-edx)
 */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>

extern bool     ERR_IsSystemWarning(int);
extern bool     ERR_IsInternWarning(int, int);
extern bool     ERR_IsExternWarning(int, int);
extern bool     ERR_IsUserWarning(int);
extern unsigned ERR_IsWarning(int);
extern bool     ERR_IsSystemError(int);
extern bool     ERR_IsInternError(int, int);
extern bool     ERR_IsExternError(int, int);
extern bool     ERR_IsUserError(int);
extern unsigned ERR_IsError(int);
extern unsigned ERRPLIB_GetFacilityID(int);
extern bool     IsDPDemo(void);

static char ob[8192]; static int op;
static void put(char c){ ob[op++]=c; }
static void puts_(const char*s){ while(*s) put(*s++); }
static void flush(void){ if(op){ if(write(1,ob,op)<0){} op=0; } }
static void hex(uint32_t v,int w){ char t[8]; int n=0; if(!v)t[n++]='0';
    while(v){int d=v&0xf; t[n++]=d<10?'0'+d:'a'+d-10; v>>=4;} for(int i=n;i<w;i++)put('0');
    for(int i=n-1;i>=0;i--)put(t[i]); }
static void dec(long v){ char t[24]; int n=0; unsigned long u; if(v<0){put('-');u=(unsigned long)(-v);}else u=v;
    if(!u)t[n++]='0'; while(u){t[n++]='0'+u%10;u/=10;} for(int i=n-1;i>=0;i--)put(t[i]); }

int main(void){
    /* --- bool classifiers over the full class-code boundary range --- */
    puts_("== bool classifiers ==\n");
    int flags[] = { 0, 1, 0xff, 0x100, 0x1234, -1 };
    for (int cls = -4; cls <= 10; cls++) {
        puts_("cls="); dec(cls);
        puts_(" SW="); dec(ERR_IsSystemWarning(cls));
        puts_(" UW="); dec(ERR_IsUserWarning(cls));
        puts_(" SE="); dec(ERR_IsSystemError(cls));
        puts_(" UE="); dec(ERR_IsUserError(cls));
        put('\n');
        for (unsigned fi = 0; fi < sizeof flags/sizeof flags[0]; fi++) {
            int f = flags[fi];
            puts_("  f="); hex((uint32_t)f,1);
            puts_(" IW="); dec(ERR_IsInternWarning(cls, f));
            puts_(" EW="); dec(ERR_IsExternWarning(cls, f));
            puts_(" IE="); dec(ERR_IsInternError(cls, f));
            puts_(" EE="); dec(ERR_IsExternError(cls, f));
            put('\n');
        }
        if (op > 7000) flush();
    }
    flush();

    /* --- ERR_IsWarning / ERR_IsError: full 32-bit return incl. setbe leak --- */
    puts_("== IsWarning/IsError leak (boundary) ==\n");
    for (int cls = -3; cls <= 8; cls++) {
        puts_("cls="); dec(cls);
        puts_(" W="); hex(ERR_IsWarning(cls),8);
        puts_(" E="); hex(ERR_IsError(cls),8);
        put('\n');
    }
    flush();
    puts_("== IsWarning/IsError leak (32-bit sweep) ==\n");
    uint32_t seed = 0x1234ABCDu;
    for (int i = 0; i < 4000; i++) {
        seed = seed*1103515245u + 12345u;
        int x = (int)seed;
        puts_("x="); hex((uint32_t)x,8);
        puts_(" W="); hex(ERR_IsWarning(x),8);
        puts_(" E="); hex(ERR_IsError(x),8);
        put('\n');
        if (op > 7000) flush();
    }
    flush();

    /* --- GetFacilityID: full table index range + out-of-range + extremes --- */
    puts_("== GetFacilityID ==\n");
    for (int e = -4; e <= 0x60; e++) {
        puts_("e="); dec(e); puts_(" -> "); hex(ERRPLIB_GetFacilityID(e),8); put('\n');
        if (op > 7000) flush();
    }
    /* extremes / wrap behaviour of the unsigned (errnum-4) index */
    int extremes[] = { 0, 1, 2, 3, 0x7fffffff, -2147483647-1, -1, 0x10000 };
    for (unsigned i = 0; i < sizeof extremes/sizeof extremes[0]; i++) {
        puts_("X="); hex((uint32_t)extremes[i],8);
        puts_(" -> "); hex(ERRPLIB_GetFacilityID(extremes[i]),8); put('\n');
    }
    flush();

    /* --- constant predicate --- */
    puts_("== IsDPDemo ==\n");
    puts_("IsDPDemo="); dec(IsDPDemo()); put('\n');
    flush();
    return 0;
}
