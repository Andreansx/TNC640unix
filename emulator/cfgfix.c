/* cfgfix.c — the config-#6 fix (LD_PRELOAD), distilled from the cfgprobe diagnosis.
 *
 * ROOT CAUSE (2026-06-24): standalone ConfigServer's config load fails because
 * `ServerHelper::IsSysFile`/`IsOemFile` return FALSE for the SYS/OEM config files.
 * They classify via `IsAncestorOf(FSystemPathname::sys()/oem(), filePath)`, and
 * sys()/oem() = `FSystemPathname::Convert("%SYS%/" / "%OEM%/")` — but the %SYS%/%OEM%
 * macro is NOT expanded standalone (the macro table is empty), so sys() returns the
 * literal "%SYS%/" and the ancestor check against "/mnt/sys/config/..." fails.
 * Consequence: in `CfgServer::ReadOneMsg`, because IsSysFile is false, `CfgServer::
 * IsJhEntity` runs and REJECTS the config message (err 0x1400010) -> `CfgServer::ReadDir`
 * returns 0 -> `MissingFile` -> ReadConfigDataDir returns 0 -> no config -> IPO `-k=NC`.
 *
 * THE FIX: classify a path as a SYS/OEM/PLCE file by its real (resolved) prefix —
 * which is exactly what sys()/oem() WOULD return if the macro expanded. Principled:
 * /mnt/sys/* IS a SYS file, /mnt/plc/* IS an OEM/PLCE file. With this, IsJhEntity is
 * skipped, the CfgJhConfigDataFiles/CfgConfigDataFiles messages are accepted, ReadDir
 * matches, and the whole load cascades (configfiles.cfg -> channel.cfg(NC) -> tnc.cfg
 * + all .atr). Verified: ReadConfigDataDir 0 -> 24, 20+ data files opened.
 *
 * Env override paths default to the standard mounts; set CFGFIX_SYS / CFGFIX_OEM to change.
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o cfgfix.so cfgfix.c
 *
 * NB (2026-06-24): the earlier version called the real IsSysFile/IsOemFile via
 * dlsym(RTLD_NEXT,...) and only overrode a FALSE result. But dlsym pulls
 * dlsym@GLIBC_2.34 (+ -ldl), and under FEX that made the WHOLE .so fail to preload
 * ("cannot be preloaded (cannot open shared object file): ignored") -> ConfigServer
 * ran WITHOUT cfgfix -> config #6 unfixed -> it never loaded/served config (so e.g.
 * AppStartMP's startup-config GetData round-trip stalled). Since the real functions
 * are ALWAYS broken standalone (sys()/oem() = the unexpanded "%SYS%/"/"%OEM%/" literal,
 * so IsAncestorOf is always false), classifying purely by the resolved prefix is the
 * CORRECT implementation of what they should return — no dlsym needed.
 */
#define _GNU_SOURCE
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
/* CFGFIX_DEBUG=1 -> log load + each IsSysFile/IsOemFile classification (to confirm the
 * interposition actually takes effect under FEX). */
static int dbg = -1;
static int dbg_on(void){ if(dbg<0){ const char*e=getenv("CFGFIX_DEBUG"); dbg = e&&e[0]=='1'; } return dbg; }

/* ── jhDataFiles FULL-LOAD patch (the AppStartMP-context fix) ────────────────────────────
 * SYMPTOM: ConfigServer ALONE loads all 15 jhDataFiles (tnc.cfg/jh.cfg/product.cfg/...), but
 * in the multi-process AppStartMP context they are SKIPPED, so AppStartMP's init config-read
 * (it needs jh.cfg/product.cfg/tnc.cfg) never completes -> Monitor::Start never runs -> the
 * batch TNC640heros.txt is never read -> the constellation never spawns.
 *
 * ROOT CAUSE (IDA, CfgServer::ReadConfigDataSet @0x229d50): the function UNCONDITIONALLY clears
 * every layer (while GetNextLayer -> ClearLayer) then calls ReadDataFiles(this, dir, START, false)
 * where START selects which registered files to (re)load. SetupDirInfo registers jhDataFiles at
 * the LOW indices [2 .. 2+N); the .atr/update lists at the HIGH indices. START is chosen at 0x229e0f:
 *     cmp byte [this+0x169], 0   ; CfgErrorParser+9 = the "config version changed / full reread" flag
 *     mov eax, 2                 ; full-load start index (covers jhDataFiles)
 *     cmovz eax, edi             ; if flag==0 -> START = ConfigDataDir (~17) => SKIP jhDataFiles
 * On the FIRST read (fresh version) the flag is set -> START=2 -> jhDataFiles load. But once the
 * version is written back, a later reread sees a MATCHING version -> flag==0 -> START=ConfigDataDir
 * -> the unconditional layer-clear wipes jhDataFiles and they are NOT reloaded. That destructive
 * reread is what leaves the AppStartMP-context config without jhDataFiles (the run is non-deterministic
 * w.r.t. the version write-back, exactly as observed: "faking chmod changes the loaded-file count").
 *
 * THE FIX: NOP the `cmovz eax, edi` (3 bytes 0f 44 c7 -> 90 90 90) so START is ALWAYS 2. The binary
 * itself uses 2 on a full read, so this just guarantees a full (jhDataFiles-inclusive) load on EVERY
 * ReadConfigDataSet, independent of the version flag. Applied as a runtime memory patch on the guest's
 * mapped libConfigSystem.so (verifies the exact bytes first; no-op if they don't match). Gated by
 * CFGFIX_FORCE_FULLLOAD (default ON; set =0 to disable). */
#define FULLLOAD_VADDR  0x229e0fUL          /* cmovz eax, edi inside CfgServer::ReadConfigDataSet */
static const unsigned char FL_EXPECT[3] = {0x0f, 0x44, 0xc7};

static int fullload_done = 0;
static void patch_fullload(void) {
    if (fullload_done) return;
    const char *e = getenv("CFGFIX_FORCE_FULLLOAD");
    if (e && e[0] == '0') { fullload_done = 1; return; }
    FILE *m = fopen("/proc/self/maps", "r");
    if (!m) return;
    unsigned long base = 0; int found = 0;
    char line[512];
    while (fgets(line, sizeof line, m)) {
        if (!strstr(line, "libConfigSystem.so")) continue;
        unsigned long s = strtoul(line, NULL, 16);
        if (!found || s < base) { base = s; found = 1; }  /* lowest mapping = ELF base (vaddr 0) */
    }
    fclose(m);
    if (!found) return;                                   /* lib not mapped yet; retry on a later call */
    fullload_done = 1;                                    /* only attempt once we can see the lib */
    unsigned char *tgt = (unsigned char *)(base + FULLLOAD_VADDR);
    if (memcmp(tgt, FL_EXPECT, 3) != 0) {
        if (dbg_on()) fprintf(stderr, "[cfgfix] FULL-LOAD patch SKIPPED: bytes at %p = %02x %02x %02x (expected 0f 44 c7)\n",
                              (void*)tgt, tgt[0], tgt[1], tgt[2]);
        return;
    }
    long pg = sysconf(_SC_PAGESIZE); if (pg <= 0) pg = 4096;
    uintptr_t pstart = (uintptr_t)tgt & ~((uintptr_t)pg - 1);
    uintptr_t pend   = ((uintptr_t)tgt + 3 + pg - 1) & ~((uintptr_t)pg - 1);
    if (mprotect((void*)pstart, pend - pstart, PROT_READ|PROT_WRITE|PROT_EXEC) != 0) {
        if (dbg_on()) fprintf(stderr, "[cfgfix] FULL-LOAD patch: mprotect failed\n");
        return;
    }
    tgt[0] = 0x90; tgt[1] = 0x90; tgt[2] = 0x90;          /* NOP NOP NOP -> START stays 2 (full load) */
    mprotect((void*)pstart, pend - pstart, PROT_READ|PROT_EXEC);
    fprintf(stderr, "[cfgfix] FULL-LOAD patch APPLIED at %p (base %#lx): jhDataFiles always reload\n",
            (void*)tgt, base);
}

__attribute__((constructor)) static void cfgfix_loaded(void){
    if(dbg_on()) fprintf(stderr,"[cfgfix] LOADED + interposing IsSysFile/IsOemFile\n");
    patch_fullload();
}

/* SYS/OEM roots are a COLON-separated list of acceptable prefixes — the resolved config
 * path is the SYS/OEM partition under whichever mount the harness used (/tmp/s symlink form
 * OR the /mnt/sys real mount). Default covers both so cfgfix works across harnesses. */
static const char *sys_pfx, *oem_pfx;
static void init_pfx(void) {
    if (sys_pfx) return;
    sys_pfx = getenv("CFGFIX_SYS"); if (!sys_pfx) sys_pfx = "/tmp/s/:/mnt/sys/";
    oem_pfx = getenv("CFGFIX_OEM"); if (!oem_pfx) oem_pfx = "/tmp/o/:/mnt/plc/";
}
/* true if `path` starts with any of the colon-separated prefixes in `list` */
static int match_pfx(const char *path, const char *list) {
    if (!path || !list) return 0;
    for (const char *p = list; *p; ) {
        const char *e = strchr(p, ':'); size_t len = e ? (size_t)(e - p) : strlen(p);
        if (len && !strncmp(path, p, len)) return 1;
        p = e ? e + 1 : p + len;
    }
    return 0;
}
/* FSystemPathname's path c-string lives at +4 (BasicString {char* begin, char* end}). */
static const char *fpath(void *fsp) {
    if (!fsp || (uintptr_t)fsp < 0x1000) return 0;
    char *s = *(char **)((char *)fsp + 4);
    if (!s || (uintptr_t)s < 0x1000 || (uintptr_t)s > 0xfffffff0) return 0;
    return s;
}

unsigned char _ZN12ServerHelper9IsSysFileERK15FSystemPathname(void *path) {
    if (!fullload_done) patch_fullload();
    init_pfx(); const char *p = fpath(path);
    unsigned char r = match_pfx(p, sys_pfx) ? 1 : 0;
    if(dbg_on()){ static int n=0; if(n++<30) fprintf(stderr,"[cfgfix] IsSysFile(\"%s\") -> %d\n", p?p:"(null)", r); }
    return r;
}
unsigned char _ZN12ServerHelper9IsOemFileERK15FSystemPathname(void *path) {
    init_pfx(); const char *p = fpath(path);
    unsigned char r = match_pfx(p, oem_pfx) ? 1 : 0;
    if(dbg_on()){ static int n=0; if(n++<30) fprintf(stderr,"[cfgfix] IsOemFile(\"%s\") -> %d\n", p?p:"(null)", r); }
    return r;
}
