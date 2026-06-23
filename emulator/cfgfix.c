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
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o cfgfix.so cfgfix.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static const char *sys_pfx, *oem_pfx;
static void init_pfx(void) {
    if (sys_pfx) return;
    sys_pfx = getenv("CFGFIX_SYS"); if (!sys_pfx) sys_pfx = "/mnt/sys/";
    oem_pfx = getenv("CFGFIX_OEM"); if (!oem_pfx) oem_pfx = "/mnt/plc/";
}
/* FSystemPathname's path c-string lives at +4 (BasicString {char* begin, char* end}). */
static const char *fpath(void *fsp) {
    if (!fsp || (uintptr_t)fsp < 0x1000) return 0;
    char *s = *(char **)((char *)fsp + 4);
    if (!s || (uintptr_t)s < 0x1000 || (uintptr_t)s > 0xfffffff0) return 0;
    return s;
}

unsigned char _ZN12ServerHelper9IsSysFileERK15FSystemPathname(void *path) {
    static unsigned char (*r)(void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN12ServerHelper9IsSysFileERK15FSystemPathname");
    unsigned char x = r(path);
    if (!x) { init_pfx(); const char *p = fpath(path); if (p && strstr(p, sys_pfx) == p) x = 1; }
    return x;
}
unsigned char _ZN12ServerHelper9IsOemFileERK15FSystemPathname(void *path) {
    static unsigned char (*r)(void *);
    if (!r) r = dlsym(RTLD_NEXT, "_ZN12ServerHelper9IsOemFileERK15FSystemPathname");
    unsigned char x = r(path);
    if (!x) { init_pfx(); const char *p = fpath(path); if (p && strstr(p, oem_pfx) == p) x = 1; }
    return x;
}
