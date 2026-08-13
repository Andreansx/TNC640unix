/* linebuf.so — make the constellation's stdout LINE-buffered (knob LINEBUF, default ON).
 *
 * WHY. Every process the launcher spawns inherits a stdout that is a FILE (>/tmp/a_appstart.log),
 * so glibc gives it a 4 KB FULLY-buffered stream. A process that is SIGKILLed at the run's timeout
 * never flushes, so anything it printed but did not fill a buffer with is LOST. That silently hid
 * the diagnostic that mattered most on 2026-08-13: startup.elf was launched with its own documented
 * "-v" (Verbose) option, which makes its FipsMain/FipsNc/FipsUI state machines announce every
 * transition ("State WaitHwInit entered." etc.) — and not one line ever reached the log, because
 * startup.elf only prints a few hundred bytes. hwserver's identical prints DID show up purely
 * because it prints dozens of KB and therefore flushed.
 *
 * This changes NO guest behaviour beyond when bytes leave the stdio buffer.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

__attribute__((constructor))
static void linebuf_init(void)
{
    const char *e = getenv("LINEBUF");
    if (e && e[0] == '0') return;
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
}
