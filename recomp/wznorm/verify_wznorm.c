/*
 * verify_wznorm.c — proves the recompiled ARM64 build of libEp90_Wznorm's
 * pure-leaf subset is byte-identical to the proprietary i386 .so.
 *
 *   (truth)   i686-linux-gnu-gcc verify_wznorm.c -lEp90_Wznorm_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_wznorm.c libEp90_Wznorm_partial_rebuilt.c
 * then diff.
 */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

extern int  GeotecToIntWkzTyp(int);
extern int  IntToGeotecWkzTyp(int);
extern int  AsciiToGeotecWkzTyp(const char *);
extern int  WerkzeugTyp(const void *, int *, int *, int *);
extern bool WZ_IsAussenWkz(const void *);

static char ob[8192]; static int op;
static void put(char c){ ob[op++]=c; }
static void puts_(const char*s){ while(*s) put(*s++); }
static void flush(void){ if(op){ if(write(1,ob,op)<0){} op=0; } }
static void hex(uint32_t v,int w){ char t[8]; int n=0; if(!v)t[n++]='0';
    while(v){int d=v&0xf; t[n++]=d<10?'0'+d:'a'+d-10; v>>=4;} for(int i=n;i<w;i++)put('0');
    for(int i=n-1;i>=0;i--)put(t[i]); }
static void dec(long v){ char t[24]; int n=0; unsigned long u; if(v<0){put('-');u=(unsigned long)(-v);}else u=v;
    if(!u)t[n++]='0'; while(u){t[n++]='0'+u%10;u/=10;} for(int i=n-1;i>=0;i--)put(t[i]); }
static void u2s(unsigned v,char*b){ int n=0; if(!v)b[n++]='0'; char t[12]; int m=0;
    while(v){t[m++]='0'+v%10;v/=10;} while(m)b[n++]=t[--m]; b[n]=0; }

int main(void){
    /* --- pure-int codecs: full 32-bit sweep + boundary --- */
    puts_("== GeotecToIntWkzTyp / IntToGeotecWkzTyp (boundary) ==\n");
    for (int v = -300; v <= 300; v++) {
        puts_("v="); dec(v);
        puts_(" G="); dec(GeotecToIntWkzTyp(v));
        puts_(" I="); dec(IntToGeotecWkzTyp(v));
        put('\n');
        if (op > 7000) flush();
    }
    flush();
    puts_("== codecs (32-bit sweep) ==\n");
    uint32_t seed = 0x2468ACEu;
    for (int i = 0; i < 4000; i++) {
        seed = seed*1103515245u + 12345u;
        int x = (int)seed;
        puts_("x="); hex((uint32_t)x,8);
        puts_(" G="); hex((uint32_t)GeotecToIntWkzTyp(x),8);
        puts_(" I="); hex((uint32_t)IntToGeotecWkzTyp(x),8);
        put('\n');
        if (op > 7000) flush();
    }
    flush();

    /* --- AsciiToGeotecWkzTyp: decimal strings within int32 range --- */
    puts_("== AsciiToGeotecWkzTyp ==\n");
    for (int v = -20; v <= 1100; v += 1) {
        char s[12]; u2s((unsigned)(v<0?-v:v), s);
        char buf[16]; int k=0; if(v<0) buf[k++]='-'; for(char*p=s;*p;)buf[k++]=*p++; buf[k]=0;
        puts_("s="); puts_(buf); puts_(" -> "); dec(AsciiToGeotecWkzTyp(buf)); put('\n');
        if (op > 7000) flush();
    }
    /* a few non-numeric / edge strings (all int32-safe) */
    const char *edge[] = { "0", "11", "999", "1000", "  42", "+7", "-5", "abc", "12x", "" };
    for (unsigned i=0;i<sizeof edge/sizeof edge[0];i++){
        puts_("e=["); puts_(edge[i]); puts_("] -> "); dec(AsciiToGeotecWkzTyp(edge[i])); put('\n');
    }
    flush();

    /* --- WerkzeugTyp + WZ_IsAussenWkz over a crafted tool struct --- */
    puts_("== WerkzeugTyp / WZ_IsAussenWkz (structured field +0xd8) ==\n");
    unsigned char tool[0x100];
    for (int field = 0; field <= 0x900; field++) {
        memset(tool, 0x5A, sizeof tool);
        memcpy(tool + 0xd8, &field, 4);
        int a,b,c;
        int r = WerkzeugTyp(tool, &a, &b, &c);
        bool aw = WZ_IsAussenWkz(tool);
        puts_("f="); hex((uint32_t)field,4);
        puts_(" r="); dec(r); puts_(" m="); dec(a); puts_(" s="); dec(b); puts_(" v="); dec(c);
        puts_(" AW="); dec(aw); put('\n');
        if (op > 7000) flush();
    }
    flush();
    puts_("== WerkzeugTyp (32-bit field sweep) ==\n");
    seed = 0x13572468u;
    for (int i = 0; i < 3000; i++) {
        seed = seed*1103515245u + 12345u;
        int field = (int)seed;
        memset(tool, 0x33, sizeof tool);
        memcpy(tool + 0xd8, &field, 4);
        int a,b,c;
        int r = WerkzeugTyp(tool, &a, &b, &c);
        bool aw = WZ_IsAussenWkz(tool);
        puts_("F="); hex((uint32_t)field,8);
        puts_(" r="); hex((uint32_t)r,8);
        puts_(" m="); hex((uint32_t)a,8); puts_(" s="); hex((uint32_t)b,8); puts_(" v="); hex((uint32_t)c,8);
        puts_(" AW="); dec(aw); put('\n');
        if (op > 7000) flush();
    }
    flush();
    return 0;
}
