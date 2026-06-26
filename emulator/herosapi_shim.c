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
/* heros_rtos's /dev/events wake bridge (weak — present only when heros_rtos is preloaded).
 * We hand it the pipe (rd,wr) + the enabled sysevent mask so its ev_send/ev_receive can make
 * /dev/events readable exactly when the kernel would, waking a select()-blocked EVHandler. */
extern void heros_evdev_register(int rd_fd, int wr_fd) __attribute__((weak));
extern void heros_evdev_setmask(int rd_fd, unsigned int mask) __attribute__((weak));
static int make_fake_events(const char *path) {
    if (events_pipe < 0) { const char *e = getenv("HEROS_EVENTS_PIPE"); events_pipe = (e && e[0]=='1'); }
    if (!events_pipe) return make_fake(path);
    int pfd[2];
    if ((int)syscall(SYS_pipe2, pfd, O_NONBLOCK | O_CLOEXEC) < 0) return make_fake(path);  /* fallback */
    events_wr_fd = pfd[1];   /* keep the write-end OPEN (a closed write-end makes the read-end EOF-ready -> spin) */
    fprintf(stderr, "[herosapi_shim] faking open(\"%s\") -> pipe rd fd %d (blocks in select until event; wr fd %d)\n",
            path, pfd[0], pfd[1]);
    remember(pfd[0]);
    /* register this thread's pipe with heros_rtos so event delivery wakes its select() */
    if (heros_evdev_register) heros_evdev_register(pfd[0], pfd[1]);
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
        /* /dev/events: ioctl(0x4502, &mask) sets the enabled sysevent mask — hand it to the
         * heros_rtos wake bridge so it signals select() only on enabled sysevents. */
        if (req == 0x4502 && arg && heros_evdev_setmask) heros_evdev_setmask(fd, *(unsigned int *)arg);
        fprintf(stderr, "[herosapi_shim] ioctl(fd=%d, req=0x%lx) -> stubbed 0\n", fd, req);
        return 0;
    }
    return (int)syscall(SYS_ioctl, fd, req, arg);
}

/* select() timeout CAP for the libbackend EVHandler dispatcher.
 * With /dev/events as a blocking pipe, EVHandlerWaitForIOEvent's select() blocks FOREVER (NULL timeout)
 * when no fd is ready -> the framework STARTUP TIMERS (FModule::CreateTimer ~12s/~54s, serviced inside the
 * event loop, NOT via heros Tm_) never fire and the boot stalls before spawning the constellation. Cap the
 * timeout (HEROSCALL_SELECT_CAP_MS, e.g. 50) ONLY for selects that include a faked /dev/events fd, so the
 * dispatcher wakes periodically, services the pending timers/messages, and proceeds — without the busy-spin
 * (the always-ready memfd) OR the forever-block (the bare pipe). This is the practical "inject the periodic
 * GUI tick" the boot's event loop expects from the kernel /dev/events driver. */
#include <sys/select.h>
#ifndef SYS__newselect
#define SYS__newselect 142   /* i386 */
#endif
static long sel_cap_ms = -2;  /* -2 uninit, -1 disabled */
int select(int nfds, fd_set *r, fd_set *w, fd_set *e, struct timeval *to) {
    if (sel_cap_ms == -2) { const char *s = getenv("HEROSCALL_SELECT_CAP_MS"); sel_cap_ms = s ? atol(s) : -1; }
    struct timeval cap;
    if (sel_cap_ms >= 0 && r) {
        int hit = 0;
        for (int i = 0; i < fake_n; i++)
            if (fake_fds[i] >= 0 && fake_fds[i] < nfds && FD_ISSET(fake_fds[i], r)) { hit = 1; break; }
        if (hit) {
            long cur = to ? (to->tv_sec * 1000L + to->tv_usec / 1000L) : -1;
            if (!to || cur > sel_cap_ms) {
                cap.tv_sec = sel_cap_ms / 1000; cap.tv_usec = (sel_cap_ms % 1000) * 1000; to = &cap;
            }
        }
    }
    return (int)syscall(SYS__newselect, nfds, r, w, e, to);
}

/* ppoll() timeout CAP — SAME rationale as select() above, for the FThread/EVHandler dispatchers that
 * use ppoll() instead of select() (e.g. skmgr's PLib frame: it blocks in ppoll(fds, nfds, NULL) on the
 * X display fd + the faked /dev/events pipe). A CROSS-PROCESS queue-notify (Guppy's SkMgrLogin ->
 * Q_SkMgr 0x313, notify 0x02000000 -> skmgr's task) sets the shared event word + futex-wakes it, but
 * ppoll waits on the PIPE, not the futex, and heros_rtos's evdev_reconcile can only poke the pipe
 * IN-PROCESS — so the cross-process notify never wakes skmgr's ppoll and the softkey login is never
 * serviced. Cap the ppoll timeout when the pollset includes a faked /dev/events fd: the dispatcher then
 * wakes every SELECT_CAP_MS, re-checks its events via Ev_receive (catching the cross-process 0x313
 * notify), and reads Q_SkMgr. Same env knob HEROSCALL_SELECT_CAP_MS as select(). */
#include <poll.h>
#include <signal.h>
#ifndef SYS_ppoll
#define SYS_ppoll 309   /* i386 __NR_ppoll */
#endif
int ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *to, const sigset_t *sigmask) {
    if (sel_cap_ms == -2) { const char *s = getenv("HEROSCALL_SELECT_CAP_MS"); sel_cap_ms = s ? atol(s) : -1; }
    struct timespec cap;
    if (sel_cap_ms >= 0 && fds) {
        int hit = 0;
        for (nfds_t i = 0; i < nfds; i++) if (is_fake(fds[i].fd)) { hit = 1; break; }
        if (hit) {
            long cur = to ? (to->tv_sec * 1000L + to->tv_nsec / 1000000L) : -1;
            if (!to || cur > sel_cap_ms) {
                cap.tv_sec = sel_cap_ms / 1000; cap.tv_nsec = (sel_cap_ms % 1000) * 1000000L; to = &cap;
            }
        }
    }
    return (int)syscall(SYS_ppoll, fds, nfds, to, sigmask, 8 /* sigsetsize = sizeof(kernel sigset_t) */);
}

/* encDir (ConfigServer's encfs config-store layer) does unshare(CLONE_NEWNS) + mount() to mount the
 * jh_int encfs view; both FAIL under FEX (ret=-1) -> encDir retry-loops and ConfigServer never reaches
 * RUNUP_COMPLETE. jh_int is a RED HERRING (config is plaintext /mnt/sys/config; cfgfix reads it directly),
 * so fake the mount-ns ops as success to stop the retry loop. Gated on HEROS_FAKE_NS=1. */
static int fake_ns = -1;
static int fake_ns_on(void){ if (fake_ns < 0){ const char *e = getenv("HEROS_FAKE_NS"); fake_ns = e && e[0]=='1'; } return fake_ns; }
int unshare(int flags){ return fake_ns_on() ? 0 : (int)syscall(SYS_unshare, flags); }
int mount(const char *src, const char *tgt, const char *fs, unsigned long fl, const void *d){
    return fake_ns_on() ? 0 : (int)syscall(SYS_mount, src, tgt, fs, fl, d); }
int umount2(const char *tgt, int fl){ return fake_ns_on() ? 0 : (int)syscall(SYS_umount2, tgt, fl); }
/* CfgErrorParser::WriteUpdVersion does FSystemPathname::SetWritePermission(version.cfg) which chmods the
 * config version file; under FEX that chmod THROWS (the C++ wrapper raises on a nonzero return), and the
 * throw ABORTS CfgServer::ReadConfigDataSet BEFORE it loads the jhDataFiles (tnc.cfg etc.) -> ConfigServer
 * serves incomplete config -> AppStartMP's init config-read never completes -> it never reaches the batch.
 * The version write-back is non-essential, so fake chmod/fchmodat success. SEPARATE gate HEROS_FAKE_CHMOD
 * (NOT HEROS_FAKE_NS): faking unshare/mount makes encDir's encfs "succeed" with an EMPTY jh_int store and
 * ConfigServer reads nothing — so we must fake chmod WITHOUT faking the mount-ns ops (let encDir fail so
 * ConfigServer falls back to reading the plaintext /mnt/sys/config that cfgfix classifies). */
static int fake_chmod = -1;
static int fake_chmod_on(void){ if (fake_chmod < 0){ const char *e = getenv("HEROS_FAKE_CHMOD"); fake_chmod = e && e[0]=='1'; } return fake_chmod || fake_ns_on(); }
int chmod(const char *path, mode_t m){ return fake_chmod_on() ? 0 : (int)syscall(SYS_chmod, path, m); }
int fchmodat(int dfd, const char *path, mode_t m, int fl){ return fake_chmod_on() ? 0 : (int)syscall(SYS_fchmodat, dfd, path, m, fl); }

/* ---------------- p_create interposer: make the constellation spawn ACTUALLY RUN under FEX -----------
 * AppStart::Platform::PCreate -> p_creates -> p_create normally does clone(CLONE_VM|VFORK 0x4111, callback)
 * then the child execve's the i386 image (e.g. winmgr.elf). Under FEX the forked-child i386-ELF execve
 * STALLS in FEX's exec re-wrap (/proc/self/fd inspection hangs). Instead: a PLAIN fork() + execve of the
 * NATIVE /usr/bin/FEXInterpreter <image> <args...> — a native exec (no FEX re-wrap), exactly like
 * AppStartMP's cat/grep helper forks that work — so the subsystem process actually launches under FEX with
 * the rootfs + i386 LD_PRELOAD (heros emulator). p_create(p1,p2,p3,int*pidout,p5,char*image,p7,...varargs):
 * the real argv = [image, p7, varargs..., NULL]; FEXInterpreter gets [FEXInterpreter, image, p7, varargs].
 * Gated HEROS_PCREATE_FEX=1. */
extern char **environ;
static int pcreate_fex = -1;
int p_create(unsigned p1, unsigned p2, unsigned p3, int *pidout, unsigned p5,
             char *image, char *p7, ...) {
    if (pcreate_fex < 0) { const char *e = getenv("HEROS_PCREATE_FEX"); pcreate_fex = e && e[0]=='1'; }
    if (!pcreate_fex) {            /* fall back to the real p_create via raw clone is hard; signal failure */
        /* not enabled: best-effort no-op failure (the gate should be ON for spawn runs) */
        if (pidout) *pidout = -1; return -1;
    }
    const char *fex = getenv("HEROS_PCREATE_FEXBIN"); if (!fex || !*fex) fex = "/usr/bin/FEXInterpreter";
    /* the nested FEXInterpreter opens the guest binary rootfs-prefixed; pass the FULL real path (like the
     * main launch /var/tmp/lr/heros5/bin/AppStartMP.elf). Translate the EXECDIRH symlink prefix:
     * HEROS_PCREATE_IMGFROM (e.g. "/tmp/b/") -> HEROS_PCREATE_IMGTO (e.g. "/var/tmp/lr/heros5/bin/"). */
    static char imgbuf[1024];
    const char *imgfrom = getenv("HEROS_PCREATE_IMGFROM"), *imgto = getenv("HEROS_PCREATE_IMGTO");
    if (image && imgfrom && imgto && strncmp(image, imgfrom, strlen(imgfrom)) == 0) {
        snprintf(imgbuf, sizeof imgbuf, "%s%s", imgto, image + strlen(imgfrom)); image = imgbuf;
    }
    char *argv[300]; int n = 0;
    argv[n++] = (char*)fex;
    if (image && *image) argv[n++] = image;
    if (p7) argv[n++] = p7;
    va_list ap; va_start(ap, p7); char *a;
    while ((a = va_arg(ap, char*)) != 0 && n < 298) argv[n++] = a;
    va_end(ap); argv[n] = 0;
    fprintf(stderr, "[pcreate] FEX-spawn: %s %s (argc=%d)\n", fex, image?image:"?", n);
    for (int i=0;i<n;i++) fprintf(stderr, "[pcreate]   argv[%d]=%s\n", i, argv[i]?argv[i]:"(null)");
    fflush(stderr);
    pid_t pid = fork();
    if (pid == 0) {
        /* the spawned subsystem must NOT re-inject the constellation set (only AppStartMP injects);
         * unset the inject env so a spawned proc seeing an AppStartMaster FmProcessState stays passive. */
        unsetenv("HEROSCALL_INJECT_FMLOAD"); unsetenv("HEROSCALL_INJECT_FMLOAD_SET");
        unsetenv("HEROSCALL_INJECT_FMLOAD_IMG"); unsetenv("HEROSCALL_INJECT_FMLOAD_PROC");
        execve(fex, argv, environ); _exit(127);
    }
    if (pid > 0 && pidout) *pidout = pid;
    return pid > 0 ? pid : -1;
}
