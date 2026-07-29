/* heurights.c — LD_PRELOAD shim for libheuseradmin's HEUTestRights (i386 guest, under FEX).
 *
 * WHY THIS EXISTS (proven chain, docs/re/appstart-subsystem-sequencing-gate-re.txt §4i):
 *   hwserver's DetectMainboard asks its own server interface for "initMode". Serving that read goes
 *   through HWSServer_::TemporaryJob::CreateJob(handle, JhUserRights::UserRight), which is
 *
 *       if ( right != 37 && !JhUserRights::Test(conn+156, conn+160, right) ) throw ServerException(3);
 *
 *   and JhUserRights::Test@libOptions+0x176a0 reduces to `HEUTestRights(ticketLo, ticketHi, name) == 1`.
 *   Inspect's required right is 27 = "NC.DataAccessServiceRead" (name table in libOptions.so,
 *   HEROS.FileOEM=0 .. Sentinel.NumberOfUserRights=37).
 *
 *   In this harness HEUTestRights always answers -1 (= DENY, since Test compares == 1):
 *     - without /tmp/__use_network_useradmin, getShm() shm_open()s "/_heusrv_shm" and the DYNAMIC path
 *       looks the right NAME up in a 64-entry table inside that segment. Our heuserver creates the
 *       segment but never populates that table, so the loop falls through to `errno=2; return -1`.
 *     - with the marker file, getShm() returns -2 and the STATIC path needs getTicketData() to find the
 *       ticket in the local list; ours is not there, so HEUTestRights returns -1 again. (Measured: the
 *       marker file alone changes nothing.)
 *   Consequence, measured end-to-end: ServerException(3) -> Inspect replies NCK_SRV_RESULT=FAILED with
 *   code 3+0x0B=14 -> GetDataSys false -> "Could not access configuration data." -> HWSMain RunUpFailed
 *   -> hwserver never sends FmProcessState(INITIALIZED) -> AppStart never dispatches the Nc subsystem.
 *
 * WHY THIS IS EMULATION, NOT A FAKE:
 *   libheuseradmin.so is a HeROS *host/OS* library (work/target/rootfs/usr/lib), not control logic. Its
 *   backing state is the user-administration database owned by heuserver — a host service this project
 *   does not reproduce, exactly like the arena, the heroscalls, or /dev/events. On the shipped free
 *   PGM-Platz the control runs with full local rights (no user administration configured), so every
 *   HEUTestRights() answers "granted". This shim supplies that shipped state; it does not alter any
 *   control decision beyond the one the real library would have made on the real image.
 *
 * SCOPE / SAFETY:
 *   Loaded ONLY when HEU_GRANT=1 (run_appstart_fex.sh prepends it just like cxathrow/fredfree), so the
 *   default build is untouched. It therefore never needs dlsym to chain to the real symbol — which is
 *   deliberate: dlsym(RTLD_NEXT) is unreliable in FEX preloads (see the cfgfix/cfg461probe notes in
 *   CLAUDE.md). Returns 1 = granted for every right, and logs the first HEURIGHTS_LOG_N distinct
 *   right names so the effect is visible and auditable in the run log rather than silent.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXSEEN 64
static char seen[MAXSEEN][64];
static int nseen = 0, logcap = -1;

/* JhUserRights::Test() is `HEUTestRights(...) == 1`, so 1 = granted. The real signature is
 * HEUTestRights(unsigned ticketLo, unsigned ticketHi, const char *rightName). */
int HEUTestRights(unsigned int lo, unsigned int hi, const char *right)
{
    if (logcap < 0) {
        const char *e = getenv("HEURIGHTS_LOG_N");
        logcap = e ? atoi(e) : MAXSEEN;
    }
    if (right && nseen < logcap && nseen < MAXSEEN) {
        int dup = 0;
        for (int i = 0; i < nseen; i++)
            if (!strcmp(seen[i], right)) { dup = 1; break; }
        if (!dup) {
            snprintf(seen[nseen], sizeof seen[0], "%s", right);
            nseen++;
            fprintf(stderr, "[heurights] GRANT \"%s\" (ticket %08x%08x)\n", right, hi, lo);
            fflush(stderr);
        }
    }
    return 1;   /* granted — the shipped PGM-Platz full-local-rights state */
}
