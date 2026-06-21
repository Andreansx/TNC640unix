/*
 * verify_bmx.c — proves the recompiled ARM64 build of libplibpp's BMX/BMP header
 * accessors is byte-identical to the proprietary i386 .so.
 *   (truth)   i686-linux-gnu-gcc verify_bmx.c -lplibpp_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_bmx.c libplibpp_bmx_partial_rebuilt.c
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>

extern unsigned bmxBmxInfo(const void *);
extern unsigned bmxBmpInfo(const void *);
extern unsigned bmxBmxVersion(const void *);
extern unsigned bmxBmpData(const void *);
extern int      CheckSizeImage(void *);

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
    static unsigned char hdr[0x40];

    /* accessors: distinct values at +0, +8, +0x10 */
    puts_("== bmx accessors ==\n");
    memset(hdr, 0, sizeof hdr);
    ((uint32_t*)hdr)[0x00/4]=0x11223344u;
    ((uint32_t*)hdr)[0x08/4]=0xAABBCCDDu;   /* version = low byte DD, data = full */
    ((uint32_t*)hdr)[0x10/4]=0x55667788u;
    puts_("info="); hex(bmxBmpInfo(hdr),8);
    puts_(" bmxinfo="); hex(bmxBmxInfo(hdr),8);
    puts_(" ver="); hex(bmxBmxVersion(hdr),8);
    puts_(" data="); hex(bmxBmpData(hdr),8); put('\n');
    flush();

    /* CheckSizeImage over header configurations (width@+4, height@+8, bpp@+0xe, *@+0x10/+0x14) */
    puts_("== CheckSizeImage ==\n");
    struct { uint32_t w, h, info10, size14; uint16_t bpp; } C[] = {
        {  0, 0, 0, 0, 0x18 },   /* width 0 */
        {  1, 1, 0, 0, 0x18 },
        {  2, 3, 0, 0, 0x18 },
        { 16,16, 0, 0, 0x18 },   /* 16x16 24bpp */
        { 17,10, 0, 0, 0x18 },   /* odd width -> row padding */
        {100,50, 0, 0, 0x18 },
        { 16,16, 0, 0, 0x08 },   /* not 24bpp -> 0 */
        { 16,16, 5, 0, 0x18 },   /* info@+0x10 != 0 -> 0 */
        { 16,16, 0, 999, 0x18 }, /* already sized -> 1, unchanged */
        { 23, 7, 0, 0, 0x18 },
    };
    for (unsigned i=0;i<sizeof C/sizeof C[0];i++){
        memset(hdr, 0, sizeof hdr);
        ((uint32_t*)hdr)[0x04/4]=C[i].w;
        ((uint32_t*)hdr)[0x08/4]=C[i].h;
        ((uint32_t*)hdr)[0x10/4]=C[i].info10;
        ((uint32_t*)hdr)[0x14/4]=C[i].size14;
        *(uint16_t*)(hdr+0x0e)=C[i].bpp;
        int r = CheckSizeImage(hdr);
        puts_("w="); dec(C[i].w); puts_(" h="); dec(C[i].h); puts_(" bpp="); hex(C[i].bpp,2);
        puts_(" -> ret="); dec(r); puts_(" size@14="); hex(((uint32_t*)hdr)[0x14/4],8); put('\n');
    }
    flush();
    return 0;
}
