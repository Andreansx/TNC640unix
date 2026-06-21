/*
 * verify_dintabs.c — proves the recompiled ARM64 build of the libEp90_Dintabs
 * thread-table functions is behaviourally identical to the original i386 .so.
 *
 * Same symbols both sides (bound to the original C++-mangled names via asm()):
 *   (truth)   i686-linux-gnu-gcc verify_dintabs.c -lEp90_Dintabs  (real i386 .so)
 *             run under qemu-i386 on the ARM64 VM              -> truth.txt
 *   (rebuilt) clang -arch arm64 verify_dintabs.c libEp90_Dintabs_rebuilt.c
 *             run natively on the M2                           -> recomp.txt
 * Then diff. Identical => equivalence proven.
 *
 * Output is hand-rolled over write(2) (no stdio) so the i386 build needs nothing
 * newer than HeROS glibc. Doubles are printed as their raw 64-bit bit pattern,
 * so the comparison is exact with zero formatting ambiguity.
 *
 * Input design (avoids any x87-vs-SSE rounding divergence):
 *   - GetNennd has NO float arithmetic (pure table load) -> identical always;
 *     swept over every valid (type, index).
 *   - freistich probes are k/20.0; every table key is a multiple of 0.05, so
 *     these hit keys EXACTLY (subtraction is 0) and the <= compares are exact.
 *   - NenndTblVgl returns a copied table entry; inputs use wide margins so the
 *     branch decisions are unambiguous in both 80-bit and 64-bit.
 */
#include <stdint.h>
#include <string.h>
#include <unistd.h>

typedef unsigned char u8;

extern double GetNennd(u8, unsigned short) asm("_Z8GetNenndht");
extern int hole_din_werte_freistich_ab(double, void**)
    asm("_Z27hole_din_werte_freistich_abdPP16din_freistich_rt");
extern int hole_din_werte_freistich_cd(double, void**)
    asm("_Z27hole_din_werte_freistich_cddPP16din_freistich_rt");
extern int hole_din_werte_freistich_ef(double, void**)
    asm("_Z27hole_din_werte_freistich_efdPP16din_freistich_rt");
extern int hole_din_werte_freistich_g(double, void**)
    asm("_Z26hole_din_werte_freistich_gdPP16din_freistich_rt");
extern double NenndTblVgl(double, double, double, int, double*, double*)
    asm("_Z11NenndTblVgldddiPdS_");

/* --- tiny output buffer, no stdio --------------------------------------- */
static char buf[8192];
static int  bp;
static void put(char c) { buf[bp++] = c; }
static void puts_(const char *s) { while (*s) put(*s++); }
static void flush(void) { write(1, buf, bp); bp = 0; }
static void hex(uint64_t v, int width) {
    char t[16]; int n = 0;
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
static void dbits(double d) {            /* raw IEEE-754 bit pattern, 16 hex */
    uint64_t u; memcpy(&u, &d, 8); hex(u, 16);
}

/* a din_freistich_rt entry is 0x30 bytes; dump them all so the returned
 * pointer's target is compared in full. */
static void dump_entry(const unsigned char *p) {
    for (int i = 0; i < 0x30; i++) hex(p[i], 2);
}

int main(void) {
    /* per-type valid index counts for GetNennd (table_size/8) */
    static const struct { u8 type; int n; } GN[] = {
        {0x09,20},{0x0a,15},{0x0b,26},{0x0d,33},{0x0e,25},
        {0x0f,27},{0x10,24},{0x11,12},{0x12,14},{0x13,7},
        {0x00,4},{0x0c,4},{0x14,4},   /* default-path types -> 0.0 */
    };
    puts_("== GetNennd ==\n");
    for (unsigned t = 0; t < sizeof(GN)/sizeof(GN[0]); t++) {
        for (int i = 0; i < GN[t].n; i++) {
            puts_("GN t="); hex(GN[t].type,2); puts_(" i="); dec(i); puts_(" -> ");
            dbits(GetNennd(GN[t].type, (unsigned short)i)); put('\n');
            if (bp > 7000) flush();
        }
    }
    flush();

    puts_("== freistich ==\n");
    for (int k = 0; k <= 1800; k++) {
        double x = k / 20.0;                 /* hits every key (mult. of 0.05) exactly */
        void *p;
        int r;
        r = hole_din_werte_freistich_ab(x, &p);
        puts_("AB k="); dec(k); puts_(" r="); dec(r);
        if (r) { puts_(" e="); dump_entry((const unsigned char*)p); } put('\n');
        r = hole_din_werte_freistich_cd(x, &p);
        puts_("CD k="); dec(k); puts_(" r="); dec(r);
        if (r) { puts_(" e="); dump_entry((const unsigned char*)p); } put('\n');
        r = hole_din_werte_freistich_ef(x, &p);
        puts_("EF k="); dec(k); puts_(" r="); dec(r);
        if (r) { puts_(" e="); dump_entry((const unsigned char*)p); } put('\n');
        r = hole_din_werte_freistich_g(x, &p);
        puts_("G  k="); dec(k); puts_(" r="); dec(r);
        if (r) { puts_(" e="); dump_entry((const unsigned char*)p); } put('\n');
        if (bp > 7000) flush();
    }
    /* a few out-of-grid probes: negative, between-keys, huge */
    static const double extra[] = { -5.0, -0.001, 0.123, 0.55, 7.0, 50.0, 99.0, 1000.0 };
    for (unsigned j = 0; j < sizeof(extra)/sizeof(extra[0]); j++) {
        double x = extra[j]; void *p; int r;
        r = hole_din_werte_freistich_ef(x, &p);
        puts_("EFx "); dbits(x); puts_(" r="); dec(r);
        if (r) { puts_(" e="); dump_entry((const unsigned char*)p); } put('\n');
        r = hole_din_werte_freistich_g(x, &p);
        puts_("Gx  "); dbits(x); puts_(" r="); dec(r);
        if (r) { puts_(" e="); dump_entry((const unsigned char*)p); } put('\n');
    }
    flush();

    puts_("== NenndTblVgl ==\n");
    {
        /* ascending nominal table + comparison table; wide margins keep the
         * loop's branch decisions unambiguous so the copied result is exact. */
        double tbl[8] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0, 128.0};
        double cmp[8] = {0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5, 0.5};
        static const struct { double tol, pitch, factor; int n; } S[] = {
            {100.0, 1.0, 1.0, 8}, {0.1, 1.0, 1.0, 8}, {10.0, 2.0, 1.5, 6},
            {1.0, 0.0, 1.0, 5},   {50.0, 1.25, 0.75, 7}, {3.0, 1.0, 10.0, 8},
        };
        for (unsigned s = 0; s < sizeof(S)/sizeof(S[0]); s++) {
            double r = NenndTblVgl(S[s].tol, S[s].pitch, S[s].factor, S[s].n, tbl, cmp);
            puts_("NV s="); dec(s); puts_(" -> "); dbits(r); put('\n');
        }
    }
    flush();
    return 0;
}
