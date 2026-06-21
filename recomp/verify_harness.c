/*
 * verify_harness.c — proves the recompiled ARM64 build of the libhdhinput
 * accessors is behaviourally identical to the original proprietary i386 .so.
 *
 * The implementation is chosen at LINK time (same symbol names both sides):
 *   (truth)   i686-linux-gnu-gcc verify_harness.c -lhdhinput   (real i386 .so)
 *             run under qemu-i386 on the ARM64 VM            -> truth.txt
 *   (rebuilt) clang -arch arm64 verify_harness.c libhdhinput_rebuilt.c
 *             run natively on the M2                         -> recomp.txt
 * Then `diff truth.txt recomp.txt`. Identical => equivalence proven.
 *
 * No printf / dlopen — output is hand-rolled over write(2) so the i386 build
 * needs nothing newer than HeROS's glibc 2.31. Inputs come from a fixed uint32
 * LCG, so i386 and arm64 see the identical descriptor sequence.
 */
#include <stdint.h>
#include <unistd.h>

typedef unsigned char u8;

/* implemented by either libhdhinput_rebuilt.c (arm64) or libhdhinput.so (i386) */
extern u8  get_pzt_export(const u8*);
extern u8  get_pzt_vorz(const u8*);
extern u8  get_pzt_igsign(const u8*);
extern u8  get_pzt_feed(const u8*);
extern u8  get_pzt_mm_inch(const u8*);
extern u8  get_pzt_hex(const u8*);
extern u8  get_pzt_float(const u8*);
extern u8  get_pzt_nkomma(int, const u8*);
extern u8  get_pzt_vkomma(int, const u8*);
extern unsigned check_pzt_range(uint32_t, const u8*);
extern void get_pzt_perm(const u8*, uint32_t*);
extern const u8 *get_pzt_pstr(const u8*);
extern int check_zt_char(const u8*, char);

/* --- tiny output buffer, no stdio --------------------------------------- */
static char buf[4096];
static int  bp;
static void put(char c) { buf[bp++] = c; }
static void puts_(const char *s) { while (*s) put(*s++); }
static void hex(uint32_t v, int width) {           /* zero-padded hex */
    char t[8]; int n = 0;
    if (v == 0) t[n++] = '0';
    while (v) { int d = v & 0xf; t[n++] = d < 10 ? '0'+d : 'a'+d-10; v >>= 4; }
    for (int i = n; i < width; i++) put('0');
    for (int i = n-1; i >= 0; i--) put(t[i]);
}
static void dec(long v) {
    char t[24]; int n = 0; unsigned long uv;
    if (v < 0) { put('-'); uv = (unsigned long)(-v); } else uv = (unsigned long)v;
    if (uv == 0) t[n++] = '0';
    while (uv) { t[n++] = '0' + uv % 10; uv /= 10; }
    for (int i = n-1; i >= 0; i--) put(t[i]);
}
static void field(const char *k, uint32_t v, int w) { puts_(k); hex(v, w); }

int main(void) {
    uint32_t seed = 0x12345678u;
    u8 d[24];
    for (int i = 0; i < 4000; i++) {
        for (int j = 0; j < 24; j++) { seed = seed*1103515245u + 12345u; d[j] = (seed >> 16) & 0xff; }
        /* bias the type byte to exercise every branch (types 0..8) on even i */
        if ((i & 1) == 0) { seed = seed*1103515245u + 12345u; d[2] = (seed >> 16) % 9u; }
        uint32_t out[3] = {0,0,0};
        get_pzt_perm(d, out);
        const u8 *ps = get_pzt_pstr(d);
        long psrel = ps ? (long)(ps - d) : -1;
        uint32_t v = d[4] | (d[5]<<8) | (d[6]<<16) | ((uint32_t)d[7]<<24);
        unsigned zc = 0;
        for (int c = 0x20; c < 0x80; c++) zc = zc*31u + (check_zt_char(d, (char)c) ? 1u : 0u);

        bp = 0;
        dec(i);
        field(" e=", get_pzt_export(d), 2);  field(" vz=", get_pzt_vorz(d), 2);
        field(" ig=", get_pzt_igsign(d), 2);  field(" fd=", get_pzt_feed(d), 2);
        field(" mi=", get_pzt_mm_inch(d), 2);
        field(" hx=", (unsigned)get_pzt_hex(d), 1); field(" fl=", (unsigned)get_pzt_float(d), 1);
        field(" n0=", get_pzt_nkomma(0,d), 2); field(" n1=", get_pzt_nkomma(1,d), 2);
        field(" v0=", get_pzt_vkomma(0,d), 2); field(" v1=", get_pzt_vkomma(1,d), 2);
        puts_(" ps="); dec(psrel);
        field(" r0=", check_pzt_range(0,d), 1); field(" rF=", check_pzt_range(0xff,d), 1);
        field(" rV=", check_pzt_range(v,d), 1);
        puts_(" pm="); hex(out[0],8); hex(out[1],8); hex(out[2],8);
        field(" zc=", zc, 8);
        put('\n');
        write(1, buf, bp);
    }
    return 0;
}
