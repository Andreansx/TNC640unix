/* cfgtrace_enable.c — LD_PRELOAD that turns on CfgStore::traceBits so the
 * standalone ConfigServer NARRATES its config-load (ConfigMsg read / Config
 * messages read, update state / File from HOME / MissingFile ...). Both the
 * global _ZN8CfgStore9traceBitsE and the setter _ZN8CfgStore12SetTraceBitsEi
 * are exported by libConfigSystem.so. The lib isn't mapped yet at preload-ctor
 * time, so we (re)try to set it on each getenv() call (cheap + idempotent),
 * which the control calls very early and often. Build:
 *   i686-linux-gnu-gcc -shared -fPIC -O2 -o cfgtrace_enable.so cfgtrace_enable.c -ldl
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdlib.h>

static int done = 0;
static void try_set(void) {
    if (done) return;
    int *tb = (int *)dlsym(RTLD_DEFAULT, "_ZN8CfgStore9traceBitsE");
    if (tb) { *tb = 0x0f; done = 1; }          /* bits 1,2,4,8 = all trace categories */
    void (*set)(int) = (void (*)(int))dlsym(RTLD_DEFAULT, "_ZN8CfgStore12SetTraceBitsEi");
    if (set) set(0x0f);
}

char *getenv(const char *name) {
    static char *(*real)(const char *) = 0;
    if (!real) real = (char *(*)(const char *))dlsym(RTLD_NEXT, "getenv");
    try_set();
    return real(name);
}
