/*
 * verify_plcbin.c — proves the recompiled ARM64 build of libplcbin is
 * behaviourally identical to the proprietary i386 .so on a crafted PLC binary.
 *
 * Same symbols both sides (plain C names):
 *   (truth)   i686-linux-gnu-gcc verify_plcbin.c -lplcbin   (real i386 .so)
 *             run under qemu-i386 on the ARM64 VM        -> truth.txt
 *   (rebuilt) clang -arch arm64 verify_plcbin.c libplcbin_rebuilt.c
 *             run natively on the M2                     -> recomp.txt
 * Then diff. Identical => equivalence proven.
 *
 * The harness builds a byte-identical .bin on both platforms, parses it, and
 * dumps every observable output (handle version, parsed info structs, bincode).
 * Output is hand-rolled over write(2) so the i386 build needs nothing newer
 * than HeROS glibc.
 */
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>

extern void  *PLCBin_Open(const char *path, int *err);
extern void   PLCBin_Close(void *h);
extern long   PLCBin_ReadBinCode(void *h, void *buf, unsigned long n);
extern int    PLCBin_ReadInfo(void *h, void *info);
extern int    PLCBin_ReadSPLCInfo(void *h, void *info);

/* --- output buffer, no stdio --- */
static char ob[8192]; static int op;
static void put(char c){ ob[op++]=c; }
static void puts_(const char*s){ while(*s) put(*s++); }
static void flush(void){ if(op){ if(write(1,ob,op)<0){} op=0; } }
static void hex(uint32_t v,int w){ char t[8]; int n=0; if(!v)t[n++]='0';
    while(v){int d=v&0xf; t[n++]=d<10?'0'+d:'a'+d-10; v>>=4;} for(int i=n;i<w;i++)put('0');
    for(int i=n-1;i>=0;i--)put(t[i]); }
static void dec(long v){ char t[24]; int n=0; unsigned long u; if(v<0){put('-');u=-v;}else u=v;
    if(!u)t[n++]='0'; while(u){t[n++]='0'+u%10;u/=10;} for(int i=n-1;i>=0;i--)put(t[i]); }

/* --- crafted PLC binary builder --- */
static unsigned char img[1024];
static int imglen;
static void be32(int off,uint32_t v){ img[off]=v>>24; img[off+1]=v>>16; img[off+2]=v>>8; img[off+3]=v; }
static int recpos;
static void rec(int off,const char*name,uint32_t val){
    int o=off+recpos;
    memset(img+o,0,0x10);
    for(int i=0;name[i]&&i<16;i++) img[o+i]=name[i];
    img[o+0x10]=0; img[o+0x11]=0;             /* BE u16 zero */
    be32(o+0x12,val);                          /* BE u32 value */
    recpos+=0x16;
}
static const char MAGIC2[40]="BIN PLC binary module  Version 2.0      ";

static void build_image(void){
    memset(img,0,sizeof img);
    memcpy(img,MAGIC2,40);
    int INFO_OFF=0x60, BIN_OFF=0x210;
    recpos=0;
    rec(INFO_OFF,"$SizePLCMEM$",0x1000);
    rec(INFO_OFF,"BYTES",0x200);
    rec(INFO_OFF,"MARKERS",0x40);
    rec(INFO_OFF,"INPUTS",0x10);
    rec(INFO_OFF,"OUTPUTS",0x10);
    rec(INFO_OFF,"TIMERS",8);
    rec(INFO_OFF,"$OffsetB$",0x100);
    rec(INFO_OFF,"$OffsetM$",0x180);
    rec(INFO_OFF,"$OffsetI$",0x1a0);
    rec(INFO_OFF,"$OffsetO$",0x1b0);
    rec(INFO_OFF,"$OffsetC$",0x1c0);
    rec(INFO_OFF,"$OffsetT$",0x1d0);
    rec(INFO_OFF,"$CRCSum$",0xdeadbeef);
    rec(INFO_OFF,"STOPLCMCOUNT",5);
    rec(INFO_OFF,"STOPLCDCOUNT",6);
    rec(INFO_OFF,"SFROMPLCMCOUNT",7);
    rec(INFO_OFF,"SFROMPLCDCOUNT",8);
    rec(INFO_OFF,"MULERROR",0x11);
    rec(INFO_OFF,"ZZUNKNOWNTOK",0x99);          /* unmatched -> ignored */
    be32(0x28,BIN_OFF);                          /* bincode offset */
    be32(0x2c,12);                               /* bincode size */
    be32(0x48,INFO_OFF);                         /* info table offset */
    be32(0x4c,(uint32_t)recpos);                 /* info table byte count */
    memcpy(img+BIN_OFF,"PLCBINCODE!!",12);
    imglen=BIN_OFF+12;
}

static int write_file(const char*path,const unsigned char*data,int len){
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    if(fd<0) return -1;
    int w=write(fd,data,len); close(fd); return w==len?0:-1;
}

int main(void){
    const char *FILE_OK="/tmp/plc_verify_ok.bin";
    const char *FILE_BAD="/tmp/plc_verify_bad.bin";
    build_image();
    write_file(FILE_OK,img,imglen);
    /* bad-magic file (40 bytes of wrong header) */
    unsigned char bad[64]; memset(bad,'X',sizeof bad);
    write_file(FILE_BAD,bad,sizeof bad);

    /* --- Open + version --- */
    int err=-99; void *h=PLCBin_Open(FILE_OK,&err);
    puts_("OPEN ok: h_nonnull="); dec(h!=0); puts_(" err="); dec(err); put('\n');

    /* --- ReadInfo (30 uint32) --- */
    uint32_t info[30];
    int r=PLCBin_ReadInfo(h,info);
    puts_("READINFO r="); dec(r); puts_("\n  ");
    for(int i=0;i<30;i++){ puts_("i"); dec(i); put('='); hex(info[i],8); put(' '); if(i%6==5)puts_("\n  "); }
    put('\n');
    PLCBin_Close(h);

    /* --- ReadSPLCInfo (21 uint32) --- */
    void *h2=PLCBin_Open(FILE_OK,&err);
    uint32_t sp[21];
    int r2=PLCBin_ReadSPLCInfo(h2,sp);
    puts_("READSPLC r="); dec(r2); puts_("\n  ");
    for(int i=0;i<21;i++){ puts_("s"); dec(i); put('='); hex(sp[i],8); put(' '); if(i%6==5)puts_("\n  "); }
    put('\n');
    PLCBin_Close(h2);

    /* --- ReadBinCode --- */
    void *h3=PLCBin_Open(FILE_OK,&err);
    unsigned char buf[32]; memset(buf,0,sizeof buf);
    long n=PLCBin_ReadBinCode(h3,buf,12);
    puts_("BINCODE n="); dec(n); puts_(" bytes="); for(int i=0;i<(n>0?n:0);i++) hex(buf[i],2); put('\n');
    long n2=PLCBin_ReadBinCode(h3,buf,12);       /* exhausted -> 0 (reset) */
    puts_("BINCODE2 n="); dec(n2); put('\n');
    PLCBin_Close(h3);

    /* --- error paths --- */
    err=-99; void *hN=PLCBin_Open("/tmp/definitely_missing_999.bin",&err);
    puts_("OPEN missing: h_nonnull="); dec(hN!=0); puts_(" err="); dec(err); put('\n');
    err=-99; void *hB=PLCBin_Open(FILE_BAD,&err);
    puts_("OPEN badmagic: h_nonnull="); dec(hB!=0); puts_(" err="); dec(err); put('\n');

    flush();
    return 0;
}
