/* renamefix.c — LD_PRELOAD shim: make cross-device rename() succeed (copy+unlink),
 * and optionally keep daemons in the foreground, for the i386 HeROS services under FEX.
 *
 * WHY: heuserver provisions its group DB by writing /tmp/__group.conf.new then rename()ing
 * it to /etc/security/group.conf. Under FEX the guest /tmp maps to the HOST /tmp (tmpfs)
 * while the rootfs /etc is on a different fs (ext4 / overlay) -> rename() returns EXDEV and
 * heuserver logs "Could not update /etc/security/groups". This shim retries EXDEV as
 * copy+unlink, so the update succeeds ("Updated /etc/security/groups"). daemon() is no-op'd
 * to 0 so `-d` services stay in the foreground (the daemon double-fork doesn't survive FEX).
 *
 * Load alongside the heros emulator (heuserver runs as REAL root after a VM restart restores
 * uid-501 -> working sudo; no fakeroot needed then):
 *   LD_PRELOAD=/lib/renamefix.so:/lib/herosapi_shim.so:/lib/heros_rtos.so
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o renamefix.so renamefix.c -ldl
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dlfcn.h>
#include <sys/stat.h>

static int xdev_copy(const char *o, const char *n) {
    int in = open(o, O_RDONLY); if (in < 0) return -1;
    struct stat st; if (fstat(in, &st)) { close(in); return -1; }
    int out = open(n, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
    if (out < 0) { close(in); return -1; }
    char b[65536]; ssize_t k;
    while ((k = read(in, b, sizeof b)) > 0) {
        char *p = b;
        while (k > 0) { ssize_t w = write(out, p, k); if (w <= 0) { close(in); close(out); return -1; } k -= w; p += w; }
    }
    close(in); close(out); unlink(o); return 0;
}

int rename(const char *o, const char *n) {
    static int (*r)(const char *, const char *); if (!r) r = dlsym(RTLD_NEXT, "rename");
    int rc = r(o, n); if (rc == 0 || errno != EXDEV) return rc; return xdev_copy(o, n);
}
int renameat(int od, const char *o, int nd, const char *n) {
    static int (*r)(int, const char *, int, const char *); if (!r) r = dlsym(RTLD_NEXT, "renameat");
    int rc = r(od, o, nd, n); if (rc == 0 || errno != EXDEV) return rc; return xdev_copy(o, n);
}
int renameat2(int od, const char *o, int nd, const char *n, unsigned f) {
    static int (*r)(int, const char *, int, const char *, unsigned); if (!r) r = dlsym(RTLD_NEXT, "renameat2");
    int rc = r ? r(od, o, nd, n, f) : -1; if (rc == 0 || errno != EXDEV) return rc; return xdev_copy(o, n);
}
int daemon(int a, int b) { (void)a; (void)b; return 0; }  /* stay foreground under FEX */
