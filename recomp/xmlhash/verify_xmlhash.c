/*
 * verify_xmlhash.c — proves the recompiled ARM64 build of libxmlreader's hash +
 * setters is byte-identical to the proprietary i386 .so.
 *   (truth)   i686-linux-gnu-gcc verify_xmlhash.c -lxmlreader_trim   (real i386, qemu-i386)
 *   (rebuilt) clang -arch arm64  verify_xmlhash.c libxmlreader_partial_rebuilt.c
 */
#include <stdint.h>
#include <unistd.h>
#include <string.h>

extern unsigned XmlKeyHashBinary(const void *, int);
extern void     XmlHashSetKey(void *, unsigned, unsigned);
extern void     XmlHashSetValueAllocator(void *, unsigned, unsigned);

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
    /* hash of fixed strings, the empty key, and high-bit (signed) bytes */
    puts_("== XmlKeyHashBinary ==\n");
    const char *K[] = { "", "a", "ab", "abc", "key", "Hello, World!",
                        "/HEIDENHAIN/CFG/Display", "node", "0123456789",
                        "\x80\x81\xff\x00\x7f", "AAAAAAAA", "the quick brown fox" };
    int lens[] = { 0, 1, 2, 3, 3, 13, 23, 4, 10, 5, 8, 19 };
    for (unsigned i=0;i<sizeof K/sizeof K[0];i++){
        puts_("h(\""); puts_(K[i]); puts_("\","); dec(lens[i]);
        puts_(")="); hex(XmlKeyHashBinary(K[i], lens[i]),8); put('\n');
    }
    /* incremental-length sweep over a fixed 256-byte buffer */
    static unsigned char buf[256];
    for (int i=0;i<256;i++) buf[i]=(unsigned char)(i*37+11);
    for (int n=0;n<=256;n++){ puts_("L"); dec(n); puts_("="); hex(XmlKeyHashBinary(buf,n),8); put(' ');
        if ((n&7)==7) put('\n'); if(op>7000)flush(); }
    put('\n'); flush();

    /* setters: single-field stores */
    puts_("== XmlHashSetKey / XmlHashSetValueAllocator ==\n");
    static unsigned char h[0x40];
    memset(h, 0xCC, sizeof h);
    XmlHashSetKey(h, 0xDEADBEEFu, 0x1234u);
    XmlHashSetValueAllocator(h, 0xCAFEBABEu, 0x5678u);
    puts_("key@0c="); hex(((uint32_t*)h)[0x0c/4],8);
    puts_(" klen@10="); hex(((uint32_t*)h)[0x10/4],8);
    puts_(" alloc@14="); hex(((uint32_t*)h)[0x14/4],8);
    puts_(" ctx@18="); hex(((uint32_t*)h)[0x18/4],8); put('\n');
    puts_(" untouched@08="); hex(((uint32_t*)h)[0x08/4],8);
    puts_(" untouched@1c="); hex(((uint32_t*)h)[0x1c/4],8); put('\n');
    flush();
    return 0;
}
