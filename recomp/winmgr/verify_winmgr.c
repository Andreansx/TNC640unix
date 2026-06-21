/*
 * verify_winmgr.c — proves the recompiled ARM64 build of libwinmgrlib's exported
 * single-level accessors is byte-identical to the proprietary i386 .so.
 *   (truth)   i686-linux-gnu-gcc verify_winmgr.c -lwinmgrlib_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_winmgr.c libwinmgrlib_partial_rebuilt.c
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>

extern int      CheckWindow(int, void *, void *);
extern unsigned WmGetMessageCount(void *);
extern unsigned WmMustConfirmEvent(void *);
extern unsigned AllocWindow(void *);
extern unsigned WmGetLastError(void *);
extern void     FreeWindow(void *);

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
    static unsigned char win[0x60], out[0x60];

    /* CheckWindow: exercise both the match (side-effect) and no-match paths */
    puts_("== CheckWindow ==\n");
    struct { uint32_t w0, w8, wc, o0; } C[] = {
        {0, 0x1111, 0xAAAA, 0x1111},   /* match: w0==0 && w8==o0 -> out+4=wc */
        {0, 0x1111, 0xAAAA, 0x2222},   /* no match: w8 != o0 */
        {1, 0x1111, 0xAAAA, 0x1111},   /* no match: w0 != 0 */
        {0, 0,      0xBEEF, 0},        /* match: 0==0 */
        {0, 0xFFFF, 0xDEAD, 0xFFFF},   /* match */
    };
    for (unsigned i=0;i<sizeof C/sizeof C[0];i++){
        memset(win,0x11,sizeof win); memset(out,0x22,sizeof out);
        ((uint32_t*)win)[0]=C[i].w0; ((uint32_t*)win)[2]=C[i].w8; ((uint32_t*)win)[3]=C[i].wc;
        ((uint32_t*)out)[0]=C[i].o0; ((uint32_t*)out)[1]=0x55555555u;
        int r = CheckWindow(0, win, out);
        puts_("ret="); dec(r); puts_(" out+4="); hex(((uint32_t*)out)[1],8); put('\n');
    }
    flush();

    /* WmGetMessageCount / WmMustConfirmEvent: single-field reads */
    puts_("== WmGetMessageCount / WmMustConfirmEvent ==\n");
    puts_("count(NULL)="); hex(WmGetMessageCount(0),8); put('\n');
    uint32_t fv[] = { 0, 1, 0x1234, 0x7fffffff, 0xffffffff, 0xdeadbeef };
    for (unsigned i=0;i<sizeof fv/sizeof fv[0];i++){
        memset(win,0x99,sizeof win);
        ((uint32_t*)win)[0x4c/4]=fv[i];
        ((uint32_t*)win)[0x38/4]=fv[i]^0x5a5a5a5au;
        puts_("v="); hex(fv[i],8); puts_(" count="); hex(WmGetMessageCount(win),8);
        puts_(" confirm="); hex(WmMustConfirmEvent(win),8); put('\n');
    }
    flush();

    /* AllocWindow: counter at +0x44, repeated bumps */
    puts_("== AllocWindow ==\n");
    memset(win,0,sizeof win); ((uint32_t*)win)[0x44/4]=0x100;
    for (int i=0;i<6;i++){ puts_("alloc="); hex(AllocWindow(win),8); puts_(" field="); hex(((uint32_t*)win)[0x44/4],8); put('\n'); }
    flush();

    /* WmGetLastError: read-and-clear at +0x3c */
    puts_("== WmGetLastError ==\n");
    memset(win,0xEE,sizeof win); ((uint32_t*)win)[0x3c/4]=0xCAFE1234u;
    puts_("err1="); hex(WmGetLastError(win),8);
    puts_(" err2="); hex(WmGetLastError(win),8);
    puts_(" field="); hex(((uint32_t*)win)[0x3c/4],8); put('\n');
    flush();

    /* FreeWindow: must not crash / no observable effect */
    puts_("== FreeWindow ==\n");
    FreeWindow(win); puts_("FreeWindow ok\n");
    flush();
    return 0;
}
