/*
 * verify_plcmap.c — proves the recompiled ARM64 build of libplcmap's pure-leaf
 * subset is byte-identical to the proprietary i386 .so.
 *   (truth)   i686-linux-gnu-gcc verify_plcmap.c -lplcmap_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_plcmap.c libplcmap_partial_rebuilt.c
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>

extern unsigned Swap_d(unsigned);
extern unsigned Swap_w(uint16_t);
extern int      UQuadCompare(unsigned, unsigned, unsigned, unsigned);
extern int      NumberOfCharacters(int);

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
    puts_("== Swap_d / Swap_w ==\n");
    uint32_t seed = 0xDEADBEEFu;
    for (int i = 0; i < 2000; i++) {
        seed = seed*1103515245u + 12345u;
        puts_("d="); hex(seed,8); puts_("->"); hex(Swap_d(seed),8);
        puts_(" w="); hex(seed&0xffff,4); puts_("->"); hex(Swap_w((uint16_t)seed),8); put('\n');
        if (op > 7000) flush();
    }
    flush();
    for (uint32_t w = 0; w < 0x10000; w += 0x101) { puts_("W"); hex(w,4); puts_("="); hex(Swap_w((uint16_t)w),8); put(' '); if(op>7000)flush(); }
    put('\n'); flush();

    puts_("== UQuadCompare ==\n");
    unsigned vals[] = { 0, 1, 2, 0x7fffffff, 0x80000000, 0xffffffff, 0xfffffffe };
    int nv = sizeof vals/sizeof vals[0];
    for (int a=0;a<nv;a++) for (int b=0;b<nv;b++) for (int c=0;c<nv;c++) for (int d=0;d<nv;d++){
        puts_("c("); hex(vals[a],8); put(':'); hex(vals[b],8); put(','); hex(vals[c],8); put(':'); hex(vals[d],8);
        puts_(")="); dec(UQuadCompare(vals[a],vals[b],vals[c],vals[d])); put('\n');
        if (op > 7000) flush();
    }
    flush();

    puts_("== NumberOfCharacters ==\n");
    int nc[] = { 0,1,9,10,99,100,999,1000,2147483647,-1,-9,-10,-99,-100,-2147483647,(-2147483647-1) };
    for (unsigned i=0;i<sizeof nc/sizeof nc[0];i++){ puts_("n="); dec(nc[i]); puts_("->"); dec(NumberOfCharacters(nc[i])); put('\n'); }
    seed = 0x1BADD00Du;
    for (int i=0;i<1500;i++){ seed=seed*1103515245u+12345u; int x=(int)seed;
        puts_("N="); hex((uint32_t)x,8); puts_("->"); dec(NumberOfCharacters(x)); put('\n'); if(op>7000)flush(); }
    flush();
    return 0;
}
