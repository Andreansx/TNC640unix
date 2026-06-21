/*
 * verify_bohrcyc.c — proves the recompiled ARM64 build of libEp90_Bohrcyc's
 * pure-integer leaf subset is byte-identical to the proprietary i386 .so.
 *
 * Same symbols both sides (plain C names):
 *   (truth)   i686-linux-gnu-gcc verify_bohrcyc.c -lEp90_Bohrcyc_trim  (real i386)
 *   (rebuilt) clang -arch arm64 verify_bohrcyc.c libEp90_Bohrcyc_partial_rebuilt.c
 * Then diff. These functions are pure integer (no FP, no external calls), so the
 * match is unconditional over all inputs.
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>

extern void     BCYC_Typisiere_Werkzeug(int, int*, unsigned*);
extern unsigned BCYC_Angetr_Werkz(const void*);

static char ob[8192]; static int op;
static void put(char c){ ob[op++]=c; }
static void puts_(const char*s){ while(*s) put(*s++); }
static void flush(void){ if(op){ if(write(1,ob,op)<0){} op=0; } }
static void hex(uint32_t v,int w){ char t[8]; int n=0; if(!v)t[n++]='0';
    while(v){int d=v&0xf; t[n++]=d<10?'0'+d:'a'+d-10; v>>=4;} for(int i=n;i<w;i++)put('0');
    for(int i=n-1;i>=0;i--)put(t[i]); }
static void dec(long v){ char t[24]; int n=0; unsigned long u; if(v<0){put('-');u=-v;}else u=v;
    if(!u)t[n++]='0'; while(u){t[n++]='0'+u%10;u/=10;} for(int i=n-1;i>=0;i--)put(t[i]); }

int main(void){
    puts_("== Typisiere_Werkzeug ==\n");
    uint32_t seed = 0xC0FFEEu;
    for (int i = 0; i < 2000; i++) {
        seed = seed*1103515245u + 12345u;
        int x = (int)seed;                       /* full 32-bit range incl. negatives */
        int a; unsigned b;
        BCYC_Typisiere_Werkzeug(x, &a, &b);
        puts_("T x="); hex((uint32_t)x,8); puts_(" a="); hex((uint32_t)a,8); puts_(" b="); hex(b,1); put('\n');
        if (op > 7000) flush();
    }
    flush();
    puts_("== Angetr_Werkz (tool byte 0..255) ==\n");
    unsigned char tool[0x100];
    for (int t = 0; t < 256; t++) {
        memset(tool, 0xA5, sizeof tool);         /* fixed filler; only +0xe0 matters */
        tool[0xe0] = (unsigned char)t;
        unsigned r = BCYC_Angetr_Werkz(tool);
        puts_("A t="); hex((uint32_t)t,2); puts_(" r="); hex(r,8); put('\n');
        if (op > 7000) flush();
    }
    flush();
    return 0;
}
