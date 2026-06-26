/* nolimit.c — interpose HeROS p_rsslimit() to a no-op.
 *
 * p_rsslimit (libheros.so.1 @HEROSLIB_500.0) installs a per-process RSS *quota*
 * (sized for the real control hardware) and a limit-exceeded handler that prints
 * "Process %d exits through p_rsslimit" and terminates the process when the quota
 * is exceeded. GuppyOemModule::Execute -> PyJHKernel::Execute calls it before
 * launching the embedded Python interpreter; under FEX the Python2.7 + GTK2
 * runtime startup immediately blows past the small quota, so the process is killed
 * BEFORE it opens/executes the GTK script (verified: 0 .py opens, exits via
 * p_rsslimit). The RSS quota is a HeROS resource-accounting feature that is
 * meaningless under userspace emulation (different memory model / no real-time
 * scheduler), so we no-op it here. Build:
 *   i686-linux-gnu-gcc -shared -fPIC -O2 -Wl,--version-script=nolimit.map -o nolimit.so nolimit.c
 * Add /lib/nolimit.so FIRST in the Guppy LD_PRELOAD so Guppy's import of
 * p_rsslimit@HEROSLIB_500.0 binds here.
 */
int p_rsslimit(int a, int b, int c, int d, void *e)
{
    (void)a; (void)b; (void)c; (void)d; (void)e;
    return 0;
}
