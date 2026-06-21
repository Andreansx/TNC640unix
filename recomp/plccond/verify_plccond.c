/*
 * verify_plccond.c — proves the recompiled ARM64 build of libplccond's
 * pure-leaf subset is byte-identical to the proprietary i386 .so.
 *
 *   (truth)   i686-linux-gnu-gcc verify_plccond.c -lplccond_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_plccond.c libplccond_partial_rebuilt.c
 * then diff.
 */
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>

extern int      toupper_ASCII(int);
extern int      tolower_ASCII(int);
extern bool     IsPathSep(char);
extern int      isNull(const char *);
extern unsigned IsStackEmpty(const void *);
extern unsigned PeekStack(const void *);
extern int      PushStack(void *, uint16_t);
extern unsigned PopStack(void *);

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
    /* --- ASCII case folding over the full byte + some wide ints --- */
    puts_("== toupper_ASCII / tolower_ASCII ==\n");
    for (int c = 0; c <= 0x1ff; c++) {
        puts_("c="); hex((uint32_t)c,3);
        puts_(" U="); hex((uint32_t)toupper_ASCII(c),8);
        puts_(" L="); hex((uint32_t)tolower_ASCII(c),8);
        put('\n');
        if (op > 7000) flush();
    }
    int wide[] = { 0x161, 0x141, -0x1f, 0x10061, 0x7fffff41, -1 };
    for (unsigned i=0;i<sizeof wide/sizeof wide[0];i++){
        puts_("w="); hex((uint32_t)wide[i],8);
        puts_(" U="); hex((uint32_t)toupper_ASCII(wide[i]),8);
        puts_(" L="); hex((uint32_t)tolower_ASCII(wide[i]),8);
        put('\n');
    }
    flush();

    /* --- IsPathSep over the full byte --- */
    puts_("== IsPathSep ==\n");
    for (int c = 0; c <= 0xff; c++) {
        if ((c & 0xf) == 0) { puts_("\n"); }
        dec(IsPathSep((char)c)); put(' ');
    }
    put('\n'); flush();

    /* --- isNull over a battery of strings --- */
    puts_("== isNull ==\n");
    const char *S[] = { "", "0", "00", "000", "0000000000", "1", "01", "10", "00x",
                        "x", "abc", "0a0", "-0", "+0", " 0", "0 ", "00000000001" };
    for (unsigned i=0;i<sizeof S/sizeof S[0];i++){
        puts_("["); puts_(S[i]); puts_("]="); dec(isNull(S[i])); put('\n');
    }
    flush();

    /* --- fixed-capacity stack: deterministic op sequence over a flat buffer --- */
    puts_("== stack ops ==\n");
    static unsigned char buf[0x1200];
    memset(buf, 0xCC, sizeof buf);
    *(int32_t *)buf = -1;                       /* empty */
    puts_("empty(buf)="); dec(IsStackEmpty(buf));
    puts_(" empty(NULL)="); dec(IsStackEmpty((void*)0));
    puts_(" peek(empty)="); hex(PeekStack(buf),8); put('\n');

    /* push 600 (past the 513 capacity to exercise the boundary) */
    for (int i = 0; i < 600; i++) {
        uint16_t v = (uint16_t)(i * 7 + 0x100);
        int ok = PushStack(buf, v);
        if (i < 5 || i > 508 && i < 520 || (i % 97) == 0) {
            puts_("push#"); dec(i); puts_(" v="); hex(v,4);
            puts_(" ok="); dec(ok); puts_(" top="); dec(*(int32_t*)buf);
            puts_(" peek="); hex(PeekStack(buf),8); put('\n');
        }
        if (op > 7000) flush();
    }
    flush();
    puts_("after pushes: top="); dec(*(int32_t*)buf);
    puts_(" empty="); dec(IsStackEmpty(buf)); put('\n');

    /* pop everything (past empty) */
    for (int i = 0; i < 600; i++) {
        unsigned v = PopStack(buf);
        if (i < 5 || i > 505 && i < 520 || (i % 97) == 0) {
            puts_("pop#"); dec(i); puts_(" v="); hex(v,8);
            puts_(" top="); dec(*(int32_t*)buf);
            puts_(" empty="); dec(IsStackEmpty(buf)); put('\n');
        }
        if (op > 7000) flush();
    }
    flush();

    /* interleaved push/pop/peek mix */
    puts_("== interleaved ==\n");
    *(int32_t *)buf = -1;
    uint32_t seed = 0x55AA1234u;
    for (int i = 0; i < 400; i++) {
        seed = seed*1103515245u + 12345u;
        int act = (seed >> 28) & 3;
        if (act == 0) { int ok = PushStack(buf, (uint16_t)seed); puts_("P"); dec(ok); }
        else if (act == 1) { unsigned v = PopStack(buf); puts_("p"); hex(v,4); }
        else if (act == 2) { puts_("k"); hex(PeekStack(buf),4); }
        else { puts_("e"); dec(IsStackEmpty(buf)); }
        puts_(":"); dec(*(int32_t*)buf); put(' ');
        if (op > 7000) flush();
    }
    put('\n'); flush();
    return 0;
}
