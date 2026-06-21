/*
 * verify_gtlib.c — proves the recompiled ARM64 build of libEp90_Gtlib's
 * single-level GTFIND_Is* classifiers is byte-identical to the proprietary i386 .so.
 *
 *   (truth)   i686-linux-gnu-gcc verify_gtlib.c -lEp90_Gtlib_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_gtlib.c libEp90_Gtlib_partial_rebuilt.c
 * then diff. Symbols bound by exact C++ mangled name via __asm__ labels.
 */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

extern bool     IsBohrung(const void *)        __asm__("_Z16GTFIND_IsBohrungP6geotec");
extern bool     IsFasRun(const void *)         __asm__("_Z15GTFIND_IsFasRunP6geotec");
extern bool     IsFreistich(const void *)      __asm__("_Z18GTFIND_IsFreistichP6geotec");
extern bool     IsEinstich(const void *)       __asm__("_Z17GTFIND_IsEinstichP6geotec");
extern bool     IsGewinde(const void *)        __asm__("_Z16GTFIND_IsGewindeP6geotec");
extern unsigned IsVariante(const void *, int)  __asm__("_Z17GTFIND_IsVarianteP6geotec9basvar_at");
extern unsigned IsFigurRucksack(const void *)  __asm__("_Z22GTFIND_IsFigurRucksackP6geotec");
extern bool     IsYEbene(unsigned)             __asm__("_Z15GTFIND_IsYEbene7plan_at");
extern unsigned IsMantel(unsigned)             __asm__("_Z15GTFIND_IsMantel7plan_at");

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
    static unsigned char node[0x80];

    /* null-pointer behaviour */
    puts_("== null ptr ==\n");
    puts_("B="); dec(IsBohrung(0)); puts_(" Fa="); dec(IsFasRun(0)); puts_(" Fr="); dec(IsFreistich(0));
    puts_(" Ei="); dec(IsEinstich(0)); puts_(" Ge="); dec(IsGewinde(0));
    puts_(" Va="); hex(IsVariante(0, 0x12345678),8); puts_(" FR="); hex(IsFigurRucksack(0),8); put('\n');

    /* sweep the tag field at +0x54 over the full classifier-relevant range */
    puts_("== tag sweep (bool classifiers + FigurRucksack leak) ==\n");
    for (int tag = -2; tag <= 0x30; tag++) {
        memset(node, 0x5A, sizeof node);
        memcpy(node + 0x54, &tag, 4);
        puts_("tag="); dec(tag);
        puts_(" B="); dec(IsBohrung(node));
        puts_(" Fa="); dec(IsFasRun(node));
        puts_(" Fr="); dec(IsFreistich(node));
        puts_(" Ei="); dec(IsEinstich(node));
        puts_(" Ge="); dec(IsGewinde(node));
        puts_(" FR="); hex(IsFigurRucksack(node),8);
        put('\n');
        if (op > 7000) flush();
    }
    flush();

    /* IsVariante: tag at +0x54 vs param2, exercising the param2 upper-byte leak */
    puts_("== IsVariante (param2 leak) ==\n");
    int p2set[] = { 0, 1, 4, 8, 9, 10, 0x15, 0x100, 0x1234, 0x7fABCD08, -1, -0x10000 };
    for (int ti = 0; ti < 6; ti++) {
        int tag = (int[]){0, 8, 0x15, 0x1234, 0x7fABCD08, -1}[ti];
        memset(node, 0x33, sizeof node);
        memcpy(node + 0x54, &tag, 4);
        for (unsigned j = 0; j < sizeof p2set/sizeof p2set[0]; j++) {
            puts_("tag="); hex((uint32_t)tag,8); puts_(" v="); hex((uint32_t)p2set[j],8);
            puts_(" -> "); hex(IsVariante(node, p2set[j]),8); put('\n');
        }
        if (op > 7000) flush();
    }
    flush();

    /* plan_at classifiers: the arg is the type code directly */
    puts_("== IsYEbene / IsMantel (plan_at) ==\n");
    for (unsigned p = 0; p <= 0x20; p++) {
        puts_("p="); dec(p); puts_(" Y="); dec(IsYEbene(p)); puts_(" M="); hex(IsMantel(p),8); put('\n');
    }
    flush();
    return 0;
}
