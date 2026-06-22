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
#include <stdlib.h>
#include <unistd.h>
#include <sys/syscall.h>

#define MAX_FAKE 64
static int fake_fds[MAX_FAKE];
static int fake_n = 0;

static int is_heros_dev(const char *p) {
    return p && (strstr(p, "herosapi") || strstr(p, "/dev/events") || strstr(p, "JHncmem"));
}
/* Optional: log interesting file opens (HEROSAPI_LOGOPEN=1) — config DB resolution. */
static int logopen = -1;
static void logo(const char *path, int fd) {
    if (logopen < 0) { const char *e = getenv("HEROSAPI_LOGOPEN"); logopen = (e && e[0]=='1'); }
    if (!logopen || !path) return;
    if (strstr(path,".cfg")||strstr(path,".atr")||strstr(path,"config")||strstr(path,":\\")||strstr(path,"SYS")||strstr(path,"PLC"))
        fprintf(stderr, "[open] %s\"%s\" -> %d\n", fd<0?"FAIL ":"", path, fd);
}
static void remember(int fd) { if (fd >= 0 && fake_n < MAX_FAKE) fake_fds[fake_n++] = fd; }
static int is_fake(int fd) {
    for (int i = 0; i < fake_n; i++) if (fake_fds[i] == fd) return 1;
    return 0;
}

/* Back the fake device with a real, sized, zeroed memfd so both read/write AND
 * mmap(MAP_SHARED, size) succeed (e.g. hessrv mmaps /dev/JHncmem, the SIK device). */
#ifndef SYS_memfd_create
#define SYS_memfd_create 356   /* i386 */
#endif
#define FAKE_DEV_SIZE (4*1024*1024)   /* 4 MB zeroed region — generous for SIK/device maps */
static int make_fake(const char *path) {
    int fd = (int)syscall(SYS_memfd_create, "heros_fakedev", 0);
    if (fd >= 0) {
        if (syscall(SYS_ftruncate, fd, FAKE_DEV_SIZE) < 0) { /* fall back below */ syscall(SYS_close, fd); fd = -1; }
    }
    if (fd < 0) fd = (int)syscall(SYS_openat, AT_FDCWD, "/dev/zero", O_RDWR, 0);  /* fallback */
    fprintf(stderr, "[herosapi_shim] faking open(\"%s\") -> fd %d (memfd %dMB)\n", path, fd, FAKE_DEV_SIZE>>20);
    remember(fd);
    return fd;
}

/* /dev/events is NOT a data device — it is the HeROS sysevent READINESS signaler the libbackend
 * EVHandler select()s on (EVHandlerWaitForIOEvent@0x3db60); when a sysevent fires the kernel driver
 * makes it readable AND ev_sends the sysevent bit, then handlesysevents@0x3d990 ev_receive()s the bit.
 * Backing it with an always-readable memfd makes select() return ready every iteration -> the GUI
 * dispatcher BUSY-SPINS on ev_receive(0x01011001,2,0) (1.5M polls/45s) and never blocks. Backing it
 * with a PIPE read-end (write-end held open, nothing written) makes it block in select() until an event
 * is injected -> the dispatcher sleeps cleanly. Env-gated (HEROS_EVENTS_PIPE=1) so RTOS procs that may
 * mmap /dev/events keep the memfd. events_wr_fd is kept for future sysevent injection (write to wake
 * select + ev_send the bit). */
#ifndef SYS_pipe2
#define SYS_pipe2 331  /* i386 */
#endif
static int events_wr_fd = -1;
static int events_pipe = -1;  /* cached HEROS_EVENTS_PIPE flag */
static int make_fake_events(const char *path) {
    if (events_pipe < 0) { const char *e = getenv("HEROS_EVENTS_PIPE"); events_pipe = (e && e[0]=='1'); }
    if (!events_pipe) return make_fake(path);
    int pfd[2];
    if ((int)syscall(SYS_pipe2, pfd, O_NONBLOCK | O_CLOEXEC) < 0) return make_fake(path);  /* fallback */
    events_wr_fd = pfd[1];   /* keep the write-end OPEN (a closed write-end makes the read-end EOF-ready -> spin) */
    fprintf(stderr, "[herosapi_shim] faking open(\"%s\") -> pipe rd fd %d (blocks in select until event; wr fd %d)\n",
            path, pfd[0], pfd[1]);
    remember(pfd[0]);
    return pfd[0];
}
static int fake_open(const char *path) {
    if (strstr(path, "/dev/events")) return make_fake_events(path);
    return make_fake(path);
}

int open(const char *path, int flags, ...) {
    if (is_heros_dev(path)) return fake_open(path);
    va_list ap; va_start(ap, flags); int m = va_arg(ap, int); va_end(ap);
    int fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, m); logo(path, fd); return fd;
}
int open64(const char *path, int flags, ...) {
    if (is_heros_dev(path)) return fake_open(path);
    va_list ap; va_start(ap, flags); int m = va_arg(ap, int); va_end(ap);
    int fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags | O_LARGEFILE, m); logo(path, fd); return fd;
}
int openat(int dfd, const char *path, int flags, ...) {
    if (is_heros_dev(path)) return fake_open(path);
    va_list ap; va_start(ap, flags); int m = va_arg(ap, int); va_end(ap);
    int fd = (int)syscall(SYS_openat, dfd, path, flags, m); logo(path, fd); return fd;
}
/* Fortified opens (_FORTIFY_SOURCE): non-variadic 2-arg form used when the compiler knows
 * there is no mode arg. hessrv opens /dev/JHncmem via __open_2. */
int __open_2(const char *path, int flags) {
    if (is_heros_dev(path)) return fake_open(path);
    int fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags, 0); logo(path, fd); return fd;
}
int __open64_2(const char *path, int flags) {
    if (is_heros_dev(path)) return fake_open(path);
    int fd = (int)syscall(SYS_openat, AT_FDCWD, path, flags | O_LARGEFILE, 0); logo(path, fd); return fd;
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
