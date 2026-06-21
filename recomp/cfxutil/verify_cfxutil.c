/*
 * verify_cfxutil.c — proves the recompiled ARM64 build of libConvertCfxNCK's
 * text/number utilities is byte-identical to the proprietary i386 .so.
 *   (truth)   i686-linux-gnu-gcc verify_cfxutil.c -lConvertCfxNCK_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_cfxutil.c libConvertCfxNCK_partial_rebuilt.c
 */
#include <stdint.h>
#include <unistd.h>

extern int      IsBinNumber(const char *);
extern unsigned BinAtol(const char *);
extern int      IsUtf8(const char *);
extern int      utf16_strlen(const uint16_t *);

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
    puts_("== IsBinNumber / BinAtol ==\n");
    const char *S[] = {
        "", "0", "1", "10", "11", "101", "%0", "%1", "%101", "%", "2", "102",
        "21", " 1", "1 ", "1 0 1", "%1 1", "0101", "1111111111111111",
        "11111111111111111111111111111111", "111111111111111111111111111111111",
        "1x", "1010a", "%%1", "  ", "%  1" };
    for (unsigned i=0;i<sizeof S/sizeof S[0];i++){
        puts_("["); puts_(S[i]); puts_("] bin="); dec(IsBinNumber(S[i]));
        puts_(" atol="); hex(BinAtol(S[i]),8); put('\n');
        if (op>7000) flush();
    }
    flush();

    puts_("== IsUtf8 ==\n");
    const char *U[] = {
        "\xef\xbb\xbf", "\xef\xbb\xbf" "ABC", "ABC", "\xef\xbb", "\xef\xbb" "X",
        "\xef" "X", "\xff\xfe", "\xef\xbb\xbe", "", "\xef" };
    for (unsigned i=0;i<sizeof U/sizeof U[0];i++){
        puts_("u#"); dec(i); puts_("="); dec(IsUtf8(U[i])); put('\n');
    }
    flush();

    puts_("== utf16_strlen ==\n");
    static const uint16_t w0[]={0};
    static const uint16_t w1[]={0x41,0};
    static const uint16_t w5[]={0x41,0x42,0x100,0xFFFF,0x20,0};
    static const uint16_t w_embed[]={0x41,0x42,0,0x43,0};   /* stops at first 0 */
    const uint16_t *W[]={w0,w1,w5,w_embed};
    for (unsigned i=0;i<sizeof W/sizeof W[0];i++){
        puts_("w#"); dec(i); puts_("="); dec(utf16_strlen(W[i])); put('\n');
    }
    flush();
    return 0;
}
