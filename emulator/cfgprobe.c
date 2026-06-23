/* cfgprobe.c — LD_PRELOAD logging interposer for the standalone ConfigServer's
 * config-load path. libConfigSystem.so is NOT -Bsymbolic, so its exported
 * intra-lib calls route through the GOT and are interposable. We log to our OWN
 * file (no dependence on the control's Trace infra, so it can't crash the
 * control) and call through to the real functions via RTLD_NEXT.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o cfgprobe.so cfgprobe.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static FILE *L;
static void lg(const char *fmt, ...) {
    if (!L) { L = fopen("/tmp/cfgprobe.log", "a"); if (!L) return; }
    va_list ap; va_start(ap, fmt); vfprintf(L, fmt, ap); va_end(ap); fflush(L);
}
/* safe-ish c-string read from a maybe-pointer (BasicString/astring first member) */
static const char *sstr(void *p) {
    if (!p || (uintptr_t)p < 0x1000) return "(nil)";
    char *s = *(char **)p;                 /* BasicString = {char* begin, char* end, ...} */
    if (!s || (uintptr_t)s < 0x1000 || (uintptr_t)s > 0xfffffff0) return "(bad)";
    return s;
}

void *_ZN9CfgServer17ReadConfigDataSetEbb(void *thiz, char a2, char a3) {
    static void *(*r)(void *, char, char);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN9CfgServer17ReadConfigDataSetEbb");
    lg(">> ReadConfigDataSet(reload=%d, a3=%d)\n", a2, a3);
    void *x = r(thiz, a2, a3);
    lg("<< ReadConfigDataSet -> %p\n", x);
    return x;
}

int _ZN9CfgServer17ReadConfigDataDirEv(void *thiz) {
    static int (*r)(void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN9CfgServer17ReadConfigDataDirEv");
    lg(">> ReadConfigDataDir(this=%p)\n", thiz);
    int x = r(thiz);
    lg("<< ReadConfigDataDir -> %d\n", x);
    return x;
}

void *_ZN8CfgStore8PathNameEiN3Lyr7LayerNrE(int idx, int layer) {
    static void *(*r)(int, int);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN8CfgStore8PathNameEiN3Lyr7LayerNrE");
    void *x = r(idx, layer);
    lg("   PathName(idx=%d, layer=%d) -> %p  ent+0='%s'  ent+4='%s'\n",
       idx, layer, x, sstr(x), sstr((char *)x + 4));
    return x;
}

unsigned char _ZNK15FSystemPathname7IsAFileEv(void *thiz) {
    static unsigned char (*r)(void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZNK15FSystemPathname7IsAFileEv");
    unsigned char x = r(thiz);
    lg("   IsAFile(this=%p path+0='%s' path+4='%s') -> %d\n",
       thiz, sstr(thiz), sstr((char *)thiz + 4), x);
    return x;
}

/* log the underlying stat syscalls so we see EXACTLY what IsAFile checks + the result */
#include <sys/stat.h>
int __xstat64(int ver, const char *path, void *buf) {
    static int (*r)(int, const char *, void *);
    if (!r) r = dlsym(RTLD_NEXT, "__xstat64");
    int x = r(ver, path, buf);
    lg("     stat('%s') -> %d\n", (path ? path : "(nil)"), x);
    return x;
}
int __lxstat64(int ver, const char *path, void *buf) {
    static int (*r)(int, const char *, void *);
    if (!r) r = dlsym(RTLD_NEXT, "__lxstat64");
    int x = r(ver, path, buf);
    lg("     lstat('%s') -> %d\n", (path ? path : "(nil)"), x);
    return x;
}
int access(const char *path, int mode) {
    static int (*r)(const char *, int);
    if (!r) r = dlsym(RTLD_NEXT, "access");
    int x = r(path, mode);
    lg("     access('%s',%d) -> %d\n", (path ? path : "(nil)"), mode, x);
    return x;
}

void _ZN9CfgServer11MissingFileERK7astring(void *thiz, void *a) {
    static void (*r)(void *, void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN9CfgServer11MissingFileERK7astring");
    lg("!! MissingFile('%s')\n", sstr(a));
    r(thiz, a);
}

/* ReadHeader: after IsAFile passes, this opens the file (GMsgInStream::init) +
 * checks MoreMessages. Returns 0 if the stream is bad / file "empty". */
int _ZN9CfgServer10ReadHeaderER7ClientPiRK15FSystemPathnameN3Lyr7LayerNrER12GMsgInStreambbb(
        void *a1, int a2, int a3, void *a4, void *a5, void *a6, char a7, unsigned char a8, char a9) {
    static int (*r)(void *, int, int, void *, void *, void *, char, unsigned char, char);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN9CfgServer10ReadHeaderER7ClientPiRK15FSystemPathnameN3Lyr7LayerNrER12GMsgInStreambbb");
    lg("   >ReadHeader(path+4='%s')\n", sstr((char *)a4 + 4));
    int x = r(a1, a2, a3, a4, a5, a6, a7, a8, a9);
    lg("   <ReadHeader -> %d\n", x);
    return x;
}

/* log opens so we see whether GMsgInStream actually opens the config file (raw)
 * after IsAFile, or whether the open is routed elsewhere (HeROS flserver). */
#include <fcntl.h>
int openat(int dirfd, const char *path, int flags, ...) {
    static int (*r)(int, const char *, int, ...);
    if (!r) r = dlsym(RTLD_NEXT, "openat");
    int x = r(dirfd, path, flags);
    if (path && (strstr(path, ".cfg") || strstr(path, ".atr") || strstr(path, "config")))
        lg("     openat('%s') -> %d\n", path, x);
    return x;
}
int open64(const char *path, int flags, ...) {
    static int (*r)(const char *, int, ...);
    if (!r) r = dlsym(RTLD_NEXT, "open64");
    int x = r(path, flags);
    if (path && (strstr(path, ".cfg") || strstr(path, ".atr") || strstr(path, "config")))
        lg("     open64('%s') -> %d\n", path, x);
    return x;
}
int open(const char *path, int flags, ...) {
    static int (*r)(const char *, int, ...);
    if (!r) r = dlsym(RTLD_NEXT, "open");
    int x = r(path, flags);
    if (path && (strstr(path, ".cfg") || strstr(path, ".atr") || strstr(path, "config")))
        lg("     open('%s') -> %d\n", path, x);
    return x;
}

/* range-string from a TstrCharacterRange {char* begin, char* end} into buf */
static const char *rng(void *p, char *buf) {
    if (!p || (uintptr_t)p < 0x1000) { buf[0] = 0; return buf; }
    char *b = *(char **)p, *e = *(char **)((char *)p + 4);
    if (!b || (uintptr_t)b < 0x1000 || e < b || (e - b) > 200) { buf[0] = 0; return buf; }
    int n = (int)(e - b); if (n > 80) n = 80;
    for (int i = 0; i < n; i++) buf[i] = b[i]; buf[n] = 0;
    return buf;
}
/* ServerHelper::IsSysFile / IsOemFile — classify the config file's path. In ReadOneMsg,
 * IsJhEntity (which rejects the message with 0x1400010) is called ONLY when IsSysFile is FALSE.
 * So IsSysFile==0 on the SYS index = the gate. */
unsigned char _ZN12ServerHelper9IsSysFileERK15FSystemPathname(void *path) {
    static unsigned char (*r)(void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN12ServerHelper9IsSysFileERK15FSystemPathname");
    unsigned char x = r(path);
    const char *p = sstr((char *)path + 4);
    /* FIX (gated CFGPROBE_FIX_SYSFILE=1): sys()/Convert returns unexpanded "%SYS%/" standalone
     * so the ancestor check wrongly fails. The file IS a SYS file if under /mnt/sys. */
    if (!x && getenv("CFGPROBE_FIX_SYSFILE") && (strstr(p, "/mnt/sys/") == p)) x = 1;
    lg("     IsSysFile(path+4='%s') -> %d\n", p, x);
    return x;
}
unsigned char _ZN12ServerHelper9IsOemFileERK15FSystemPathname(void *path) {
    static unsigned char (*r)(void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN12ServerHelper9IsOemFileERK15FSystemPathname");
    unsigned char x = r(path);
    const char *p = sstr((char *)path + 4);
    if (!x && getenv("CFGPROBE_FIX_SYSFILE") && (strstr(p, "/mnt/plc/") == p)) x = 1;
    lg("     IsOemFile(path+4='%s') -> %d\n", p, x);
    return x;
}

/* ServerHelper::ReadMessage — parse one message. Returns char (0=parse failed).
 * After it, *a2 = the parsed GMessage (or null). */
char _ZN12ServerHelper11ReadMessageER12GMsgInStreamRSt10unique_ptrI8GMessageSt14default_deleteIS3_EEP13GMsgErrorListP10GMsgStringb(
        void *a1, void **a2, void *a3, void *a4, char a5) {
    static char (*r)(void *, void **, void *, void *, char);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN12ServerHelper11ReadMessageER12GMsgInStreamRSt10unique_ptrI8GMessageSt14default_deleteIS3_EEP13GMsgErrorListP10GMsgStringb");
    char x = r(a1, a2, a3, a4, a5);
    lg("     ReadMessage -> %d  (*msg=%p)\n", (int)x, a2 ? *a2 : (void *)0);
    return x;
}
/* CfgServer::IsForbiddenFromFile — returns nonzero error code if the message is rejected. */
int _ZN9CfgServer19IsForbiddenFromFileERK7astringS2_iN3Lyr7LayerNrElRSt10unique_ptrI8GMessageSt14default_deleteIS6_EE(
        void *a1, void *name, void *key, int a4, int layer, long a6, void **msg) {
    static int (*r)(void *, void *, void *, int, int, long, void **);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN9CfgServer19IsForbiddenFromFileERK7astringS2_iN3Lyr7LayerNrElRSt10unique_ptrI8GMessageSt14default_deleteIS6_EE");
    int x = r(a1, name, key, a4, layer, a6, msg);
    lg("     IsForbiddenFromFile(name='%s') -> %d  (*msg=%p)\n", sstr(name), x, msg ? *msg : (void *)0);
    return x;
}

/* operator==(TstrCharacterRange&, TstrCharacterRange&) — the config entity-name match.
 * Log only compares where one side looks like a config type name (contains "Cfg"/"Config"). */
unsigned char _ZeqIcEbRK18TstrCharacterRangeIT_ES4_(void *a, void *b) {
    static unsigned char (*r)(void *, void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZeqIcEbRK18TstrCharacterRangeIT_ES4_");
    unsigned char x = r(a, b);
    char ba[96], bb[96];
    rng(a, ba); rng(b, bb);
    if (strstr(ba, "Cfg") || strstr(bb, "Cfg") || strstr(ba, "Config") || strstr(bb, "Config"))
        lg("     cmp('%s' == '%s') -> %d\n", ba, bb, x);
    return x;
}
