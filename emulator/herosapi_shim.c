/*
 * herosapi_shim.c — LD_PRELOAD interpose that stubs the HeROS kernel API
 * (/dev/herosapi, /dev/events) so the i386 control's libheros_init can proceed
 * when running under user-mode translation (qemu-i386 / box86 / FEX) on a host
 * that has no heros.ko. This is the "no kernel services" stub (doc 16, option 2):
 * it lets us measure the NEXT runtime dependency past the kernel-API open.
 *
 * Uses RAW SYSCALLS (no dlsym/libdl) so the .so does not pull a newer glibc
 * symbol version than the HeROS i386 libc (2.31) provides.
 *
 * Build (i386, inside the ARM64 VM):
 *   i686-linux-gnu-gcc -shared -fPIC -O2 -fno-stack-protector \
 *       herosapi_shim.c -o herosapi_shim.so
 * Use:
 *   qemu-i386 -L $ROOTFS -E LD_LIBRARY_PATH=/heros5/bin \
 *             -E LD_PRELOAD=/tmp/herosapi_shim.so $ROOTFS/lib/ld-linux.so.2 <binary>
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/syscall.h>

#define MAX_FAKE 64
static int fake_fds[MAX_FAKE];
static int fake_n = 0;

static int is_heros_dev(const char *p) {
    return p && (strstr(p, "herosapi") || strstr(p, "/dev/events"));
}
static void remember(int fd) { if (fd >= 0 && fake_n < MAX_FAKE) fake_fds[fake_n++] = fd; }
static int is_fake(int fd) {
    for (int i = 0; i < fake_n; i++) if (fake_fds[i] == fd) return 1;
    return 0;
}

/* Back the fake device with a real, harmless fd so read/write/close behave. */
static int make_fake(const char *path) {
    int fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/zero", O_RDWR, 0);
    fprintf(stderr, "[herosapi_shim] faking open(\"%s\") -> fd %d\n", path, fd);
    remember(fd);
    return fd;
}

int open(const char *path, int flags, ...) {
    if (is_heros_dev(path)) return make_fake(path);
    va_list ap; va_start(ap, flags); int m = va_arg(ap, int); va_end(ap);
    return (int)syscall(SYS_openat, AT_FDCWD, path, flags, m);
}
int open64(const char *path, int flags, ...) {
    if (is_heros_dev(path)) return make_fake(path);
    va_list ap; va_start(ap, flags); int m = va_arg(ap, int); va_end(ap);
    return (int)syscall(SYS_openat, AT_FDCWD, path, flags | O_LARGEFILE, m);
}
int openat(int dfd, const char *path, int flags, ...) {
    if (is_heros_dev(path)) return make_fake(path);
    va_list ap; va_start(ap, flags); int m = va_arg(ap, int); va_end(ap);
    return (int)syscall(SYS_openat, dfd, path, flags, m);
}

/* Answer every ioctl on a faked device fd with success. */
int ioctl(int fd, unsigned long req, ...) {
    va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
    if (is_fake(fd)) {
        fprintf(stderr, "[herosapi_shim] ioctl(fd=%d, req=0x%lx) -> stubbed 0\n", fd, req);
        return 0;
    }
    return (int)syscall(SYS_ioctl, fd, req, arg);
}
