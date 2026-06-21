/*
 * verify_file.c — proves the recompiled ARM64 build of libfile's exported
 * pure-leaf subset is byte-identical to the proprietary i386 .so.
 *   (truth)   i686-linux-gnu-gcc verify_file.c -lfile_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_file.c libfile_partial_rebuilt.c
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>

extern unsigned BitFieldTst(const void *, int);
extern int      IsNcFile(int);
extern int      IsAscFile(int);
extern unsigned FlServerListSize(const void *);
extern unsigned read_mminch(void);

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
    /* BitFieldTst over a known 16-byte bit array, bit index incl. negatives */
    puts_("== BitFieldTst ==\n");
    static unsigned char bits[16] = {
        0x00,0xFF,0xA5,0x5A,0x01,0x80,0x12,0x34,0xCC,0x33,0xF0,0x0F,0xDE,0xAD,0xBE,0xEF };
    for (int bit = -16; bit < 128; bit++) {
        puts_("b="); dec(bit); puts_("="); hex(BitFieldTst(bits, bit),8); put(' ');
        if ((bit & 7) == 7) put('\n');
        if (op > 7000) flush();
    }
    put('\n'); flush();

    /* IsNcFile / IsAscFile over the tag range */
    puts_("== IsNcFile / IsAscFile ==\n");
    for (int t = -4; t <= 0x32; t++) {
        puts_("t="); dec(t); puts_(" nc="); dec(IsNcFile(t)); puts_(" asc="); dec(IsAscFile(t)); put('\n');
        if (op > 7000) flush();
    }
    flush();

    /* FlServerListSize: single-field read at +4 */
    puts_("== FlServerListSize ==\n");
    static unsigned char node[0x20];
    uint32_t szs[] = { 0, 1, 0x1234, 0x7fffffff, 0xffffffff, 42 };
    for (unsigned i=0;i<sizeof szs/sizeof szs[0];i++){
        memset(node, 0x66, sizeof node);
        memcpy(node+4, &szs[i], 4);
        puts_("s="); hex(szs[i],8); puts_(" -> "); hex(FlServerListSize(node),8); put('\n');
    }
    flush();

    puts_("== read_mminch ==\n");
    puts_("read_mminch="); hex(read_mminch(),8); put('\n');
    flush();
    return 0;
}
