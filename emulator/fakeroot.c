/* fakeroot.c — LD_PRELOAD shim so the i386 HeROS system daemons (heuserver et al.)
 * run UNPRIVILEGED on ARM64 under FEX-Emu when real root is unavailable.
 *
 * WHY: FEX runs the control's i386 binaries fine as the *unprivileged* user, but
 * NOT under sudo (the lima VM's uid-501 host-mapping is unresolvable -> sudo and
 * other permission-dependent paths break). heuserver hard-checks geteuid()==0 and
 * performs privileged ops (chown/chmod/setgroups). This shim makes it BELIEVE it is
 * root (so it runs its credential setup) and no-ops the file-ownership ops it can't
 * actually do unprivileged. Load it FIRST in LD_PRELOAD, ahead of the heros emulator:
 *   LD_PRELOAD=/lib/fakeroot.so:/lib/herosapi_shim.so:/lib/heros_rtos.so
 *
 * RESULT (2026-06-22): with this + a my-user-owned FEX overlay rootfs + the heros
 * emulator (fresh /dev/shm names so the unprivileged user can create them), heuserver
 * runs its FULL setup observably under FEX on ARM64 — parses the NC/PLC/HEROS legacy
 * roles, provisions function-users, generates /etc/sysconfig/heuseradmin/heuseradmin.cfg.
 * The remaining failures are file WRITES (passwd.new / keyfile / group.conf) that the
 * VM's unresolvable-uid-501 permission degradation blocks; a fresh VM (uid 501 resolves
 * -> real sudo/root) lets them succeed and heuserver bind. Build:
 *   i686-linux-gnu-gcc -shared -fPIC -O2 -o fakeroot.so fakeroot.c
 */
#include <sys/types.h>
#include <stddef.h>

uid_t getuid(void)  { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getgid(void)  { return 0; }
gid_t getegid(void) { return 0; }
int setuid(uid_t u)  { (void)u; return 0; }
int setgid(gid_t g)  { (void)g; return 0; }
int seteuid(uid_t u) { (void)u; return 0; }
int setegid(gid_t g) { (void)g; return 0; }
int setgroups(size_t n, const gid_t *l) { (void)n; (void)l; return 0; }
int initgroups(const char *u, gid_t g)  { (void)u; (void)g; return 0; }
int chown(const char *p, uid_t u, gid_t g)  { (void)p; (void)u; (void)g; return 0; }
int fchown(int f, uid_t u, gid_t g)         { (void)f; (void)u; (void)g; return 0; }
int lchown(const char *p, uid_t u, gid_t g) { (void)p; (void)u; (void)g; return 0; }
int chmod(const char *p, mode_t m)  { (void)p; (void)m; return 0; }
int fchmod(int f, mode_t m)         { (void)f; (void)m; return 0; }
