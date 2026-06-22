/* arena_stub.c — bridge the one HEIDENHAIN-custom glibc symbol the RTOS compute libs need.
 *
 * The control's libheros.so.1 references `arena_exclusive@GLIBC_2.0`, a WEAK malloc-arena symbol
 * that HEIDENHAIN's PATCHED control glibc-2.31 defines but the modern i386 glibc does NOT. To run
 * the compute processes (ConfigServer/IPO) under FEX we must use the modern glibc (the bare
 * control glibc-2.31 segfaults under FEX), so this no-op bridge supplies the missing symbol.
 *
 * Build (i386, in VM):
 *   i686-linux-gnu-gcc -shared -fPIC -O2 -Wl,--version-script=arena.map -o arena_stub.so arena_stub.c
 * Use: LD_PRELOAD it FIRST (before glibc) so libheros resolves arena_exclusive@GLIBC_2.0 here.
 *
 * No-op is sufficient for load + init + the RTOS run-up: arena_exclusive locks malloc arenas for
 * exclusive access; the modern glibc malloc still works, only the (concurrency-time) exclusivity
 * guarantee is dropped, which does not matter for ConfigServer's single-threaded init/run-up.
 */
int arena_exclusive(void) { return 0; }
