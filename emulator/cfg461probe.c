/* cfg461probe.c — LD_PRELOAD tracer for the CfgServer::OnWriteNew (0x170461) fork path.
 *
 * GOAL (Gate 1): Fred sends CfgWriteNew (tag 0x170461, 141B) to CfgServerQueue(0x303) and BLOCKS for a
 * CfgWriteDone reply on its reply-q "4-000010eCfgM"(0x339). Statically RE'd path:
 *   DispatchMessage(0x235eb0) -> OnWriteNew(0x2270f0)
 *     -> ClientIsConnected -> CheckNotification(0x191830, a4=true)   [EARLY-OUT if this+232 set]
 *     -> GetWrittenLayer(0x20a130)                                    [resolve writable layer]
 *     -> StoreWriteRequest(0x210240)                                  [enqueue WriteReqData at this+260]
 *     -> WriteData(0x218870) -> WriteElementFromWritequeue(0x21e540)
 *          -> InformOrFinish(0x2230e0) = THE FORK:
 *               FINISH -> WriteAndInformCheckError(0x222fa0)/WriteFinish(0x21e7a0)
 *                         -> SendRequestDone(0x210d10) = CfgWriteDone to reply-q  [Fred unblocks]
 *               INFORM -> SendNotify(0x216af0) to a subscriber Client, DEFER the reply until
 *                         that subscriber acks CfgNotifyDone -> OnNotifyDone(0x233050)
 *                         -> PostPendings(0x186270) drains + finishes.
 * We LOG which path fires to learn WHICH of:
 *   (a) INFORM to a subscriber trimmed out of bar3            -> SendNotify fires, OnNotifyDone NEVER
 *   (b) INFORM to a present subscriber whose ack mis-routes   -> SendNotify fires, OnNotifyDone NEVER
 *   (c) layer resolution fails                                -> GetWrittenLayer returns 99/100/err
 *   (d) FINISH but CfgWriteDone reply lost to Fred's 0x339    -> SendRequestDone fires, Fred still hangs
 * We can classify the fork from SendNotify(INFORM) vs SendRequestDone(FINISH) + OnNotifyDone(ack) WITHOUT
 * interposing the struct-returning WriteData/InformOrFinish (whose hidden-retptr ABI is error-prone).
 *
 * SAFETY (learned the hard way): an earlier version broke ConfigServer's config LOAD (the version write-back
 * exercises this same write machinery) -> the AppStartMP config round-trip stalled -> the constellation never
 * spawned. Fixes: (1) NO struct-return interposers; (2) log to STDERR (goes to a_cfgsrv.log; the file path
 * was FEX-redirected into the rootfs /tmp and silently lost); (3) gate ALL deep-function logging on
 * `fred_write_seen` (set at the first OnWriteNew) so the probe is a bare flag-check + transparent forward
 * during config load and only becomes chatty once Fred's write arrives.
 *
 * FEX-SAFE: NO dlsym / NO -ldl (that makes the whole .so fail to preload under FEX — see cfgfix.c).
 * Resolve real bodies via /proc/self/maps base + known file vaddrs (cfgfix uses the identical technique for
 * its FULL-LOAD patch, proving base+vaddr is correct under FEX); calling base+vaddr bypasses the GOT so no
 * recursion. Our preloaded symbols win the GOT for libConfigSystem.so's intra-lib calls (lib is NOT -Bsymbolic).
 *
 * Build: i686-linux-gnu-gcc -shared -fPIC -O2 -o cfg461probe.so cfg461probe.c
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

static int fred_write_seen = 0;   /* set at first OnWriteNew; gates deep-function logging */

static void lg(const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    fputs("[cfg461] ", stderr); vfprintf(stderr, fmt, ap); fflush(stderr);
    va_end(ap);
}

/* resolve libConfigSystem.so base (lowest mapping = ELF vaddr 0) from /proc/self/maps */
static uintptr_t g_base;
static uintptr_t base(void) {
    if (g_base) return g_base;
    FILE *m = fopen("/proc/self/maps", "r"); if (!m) return 0;
    uintptr_t b = 0; int found = 0; char line[512];
    while (fgets(line, sizeof line, m)) {
        if (!strstr(line, "libConfigSystem.so")) continue;
        uintptr_t s = (uintptr_t)strtoul(line, NULL, 16);
        if (!found || s < b) { b = s; found = 1; }
    }
    fclose(m);
    if (found) { g_base = b; lg("libConfigSystem.so base=%#lx\n", (unsigned long)b); }
    return g_base;
}
#define REAL(off, T) ((T)(base() + (uintptr_t)(off)))

/* safe c-string read from a maybe-astring/BasicString ({char* begin, char* end, ...}) */
static const char *sstr(const void *p) {
    if (!p || (uintptr_t)p < 0x1000) return "(nil)";
    char *s = *(char *const *)p;
    if (!s || (uintptr_t)s < 0x1000 || (uintptr_t)s > 0xfffffff0) return "(bad)";
    return s;
}

/* this+232 (= dword index 58) = "Notification in progress" flag (CheckNotification@0x191830).
 * When set, CheckNotification early-outs (returns 1) so OnWriteNew DEFERS the write with NO reply. */
static int notif_flag(const void *thiz) {
    if (!thiz || (uintptr_t)thiz < 0x1000) return -1;
    return *((const int *)thiz + 58);
}

/* Dump the SyncMap (std::map<astring,Access> at this+212): _Rb_tree header at this+216, leftmost node at
 * this+224, node_count at this+232. Each node = {color,parent,left,right}(16B) then pair{astring key,Access}.
 * The astring key at node+16 (its char* begin = the subscriber name) IS the stuck subscriber's identity. */
static void *rb_next(void *x, void *hdr) {
    void *r = *((void *const *)((char *)x + 12));           /* right */
    if (r) { void *l; x = r; while ((l = *((void *const *)((char *)x + 8)))) x = l; return x; }
    void *p = *((void *const *)((char *)x + 4));            /* parent */
    while (p && p != hdr && x == *((void *const *)((char *)p + 12))) { x = p; p = *((void *const *)((char *)x + 4)); }
    return p;
}
static void dump_syncmap(void *thiz) {
    if (!thiz || (uintptr_t)thiz < 0x1000) return;
    int cnt = notif_flag(thiz);
    void *hdr = (char *)thiz + 216;                          /* &_M_header */
    void *n = *((void *const *)((char *)thiz + 224));        /* leftmost */
    lg("   SyncMap: %d subscriber(s) awaiting CfgNotifyDone ack:\n", cnt);
    for (int i = 0; n && n != hdr && i < 20; i++, n = rb_next(n, hdr))
        lg("      [%d] subscriber='%s'  (Access@%p)\n", i, sstr((char *)n + 16), (char *)n + 16 + 12);
}

/* ── OnWriteNew(CfgWriteNew&) — the write handler entry (only fires for CfgWriteNew/0x170461) ── */
void _ZN9CfgServer10OnWriteNewER11CfgWriteNew(void *thiz, void *cfgwritenew) {
    fred_write_seen++;
    lg(">> OnWriteNew #%d (this=%p, notif_in_progress[this+232]=%d)\n", fred_write_seen, thiz, notif_flag(thiz));
    if (notif_flag(thiz) != 0) dump_syncmap(thiz);
    REAL(0x2270f0, void(*)(void*,void*))(thiz, cfgwritenew);
    lg("<< OnWriteNew #%d\n", fred_write_seen);
}

/* ── CheckNotification(Client&, GMessage const*, bool) — nonzero return = EARLY-OUT, NO reply ──
 * Returns 1 when: this+232 set (notif in progress), OR a client-write in progress, OR restricted-reconfig. */
unsigned char _ZN9CfgServer17CheckNotificationERK7ClientPRK8GMessageb(void *thiz, void *clientp, void *gmsg, char b) {
    int nf = notif_flag(thiz);
    unsigned char x = REAL(0x191830, unsigned char(*)(void*,void*,void*,char))(thiz, clientp, gmsg, b);
    if (fred_write_seen)
        lg("   CheckNotification(a4=%d, notif_in_progress[this+232]=%d) -> %d   %s\n",
           b, nf, x, x ? "(EARLY-OUT: defer, no reply!)" : "");
    return x;
}

/* ── GetWrittenLayer(ClientP&, CfgResult&, bool, GMsgString&) — resolved writable layer or err sentinel ── */
int _Z15GetWrittenLayerRK7ClientPR9CfgResultbRK10GMsgString(void *clientp, void *cfgresult, char b, void *gmsgstr) {
    int x = REAL(0x20a130, int(*)(void*,void*,char,void*))(clientp, cfgresult, b, gmsgstr);
    if (fred_write_seen) {
        int err = cfgresult ? *((int *)cfgresult + 2) : -999;
        lg("   GetWrittenLayer(byRef=%d) -> layer=%d  (CfgResult.err=%d)%s\n",
           b, x, err, (x == 99 || x == 100) ? "  <-- ERR sentinel" : "");
    }
    return x;
}

/* ── StoreWriteRequest(WriteReqData&) — ENQUEUE. WriteReqData+0 = requesting-client name astring ── */
void *_ZN9CfgServer17StoreWriteRequestER12WriteReqData(void *thiz, void *wrd) {
    if (fred_write_seen) lg("   StoreWriteRequest(client='%s')\n", sstr(wrd));
    return REAL(0x210240, void*(*)(void*,void*))(thiz, wrd);
}

/* ── WriteFinish(ClientP&) — completes a client's write queue ── */
unsigned char _ZN9CfgServer11WriteFinishER7ClientP(void *thiz, void *clientp) {
    unsigned char x = REAL(0x21e7a0, unsigned char(*)(void*,void*))(thiz, clientp);
    if (fred_write_seen) lg("     WriteFinish -> %d\n", x);
    return x;
}

/* ── WriteAndInformCheckError(ClientP&, CfgResult const&, bool) — the check-error tail (FINISH-ish) ── */
void _ZN9CfgServer24WriteAndInformCheckErrorER7ClientPRK9CfgResultb(void *thiz, void *clientp, void *cfgresult, char b) {
    if (fred_write_seen) lg("     WriteAndInformCheckError(b=%d)\n", b);
    REAL(0x222fa0, void(*)(void*,void*,void*,char))(thiz, clientp, cfgresult, b);
}

/* ── SendNotify(ClientP&, CfgNotify*&, bool) — INFORM branch: notify a subscriber Client ── */
unsigned int _ZN9CfgServer10SendNotifyERK7ClientPR9CfgNotifyb(void *thiz, void *clientp, void *cfgnotify, char b) {
    /* UN-GATED: the config-change notify that involves .EditThreadNotify happens before Fred's write. */
    void *cl = (clientp && (uintptr_t)clientp > 0x1000) ? *((void *const *)((char *)clientp + 12)) : 0;
    lg("   ** SendNotify(delayed=%d)  subscriberClientP=%p Client=%p  (SyncMap count[this+232]=%d)\n",
       b, clientp, cl, notif_flag(thiz));
    return REAL(0x216af0, unsigned int(*)(void*,void*,void*,char))(thiz, clientp, cfgnotify, b);
}

/* ── SendNotifySync(OneMapIterator&, bool) — the SYNC broadcast: sends CfgNotifySync to a subscriber in
 * the SyncMap and awaits its CfgNotifyDone. UN-GATED: this is what populates "notification in progress"
 * BEFORE Fred's write, so we log it always. Subscriber name = *(a2+4)=Access, Access+16 = client astring. */
void _ZN9CfgServer14SendNotifySyncER14OneMapIteratorISt3mapI7astring6AccessSt4lessIS2_ESaISt4pairIKS2_S3_EEES2_S3_Eb(void *a1, void *a2, unsigned char hold) {
    const char *sub = "(?)";
    if (a2 && (uintptr_t)a2 > 0x1000) {
        void *access = *((void *const *)((char *)a2 + 4));
        if (access && (uintptr_t)access > 0x1000) sub = sstr((char *)access + 16);
    }
    lg("** SendNotifySync -> subscriber='%s' hold=%d  (SyncMap count[this+232]=%d)  [populates notif-in-progress]\n",
       sub, hold, notif_flag(a1));
    REAL(0x232f50, void(*)(void*,void*,unsigned char))(a1, a2, hold);
}

/* ── SendRequestDone(CfgResult const&, astring const&) — FINISH: CfgWriteDone to reply target ── */
void _ZN9CfgServer15SendRequestDoneERK9CfgResultRK7astring(void *thiz, void *cfgresult, void *replyto) {
    if (fred_write_seen) lg("   ## SendRequestDone(reply_to='%s')  [FINISH: CfgWriteDone sent]\n", sstr(replyto));
    REAL(0x210d10, void(*)(void*,void*,void*))(thiz, cfgresult, replyto);
}

/* ── OnNotifyDone(CfgNotifyDone const&, bool) — THE ACK from a subscriber (INFORM completion) ── */
void _ZN9CfgServer12OnNotifyDoneERK13CfgNotifyDoneb(void *thiz, void *notifydone, char b) {
    /* UN-GATED: the missing ack may be expected BEFORE Fred's write; log it always with the SyncMap count. */
    lg("!! OnNotifyDone(b=%d)  (SyncMap count[this+232] before=%d)  [an ACK arrived]\n", b, notif_flag(thiz));
    REAL(0x233050, void(*)(void*,void*,char))(thiz, notifydone, b);
    lg("!! OnNotifyDone done (SyncMap count[this+232] after=%d)\n", notif_flag(thiz));
}

/* ── PostPendings() — drains deferred replies after an ack ── */
void _ZN9CfgServer12PostPendingsEv(void *thiz) {
    if (fred_write_seen) lg("   .. PostPendings()\n");
    REAL(0x186270, void(*)(void*))(thiz);
}

/* ── this+232 (SyncMap count) 0->N TRANSITION DETECTORS ──
 * The subscriber is inserted into the SyncMap by SOME message handler before Fred's write; SendNotifySync
 * does NOT do it. These wrap the write/subscribe/reconfig handlers and log when the count RISES, naming the
 * culprit operation. All are `void On<X>(Cfg<X> const&)` = clean 2-arg member ABI unless noted. */
#define TRACK2(mangled, off, tag) \
void mangled(void *thiz, void *msg) { \
    int b = notif_flag(thiz); \
    REAL(off, void(*)(void*,void*))(thiz, msg); \
    int a = notif_flag(thiz); \
    if (a > b) lg("*** SyncMap count %d->%d in " tag "  (client0='%s')\n", b, a, sstr(msg)); \
}
TRACK2(_ZN9CfgServer18OnNotifyForObjectsERK19CfgNotifyForObjects, 0x211e50, "OnNotifyForObjects")
TRACK2(_ZN9CfgServer15OnNotifyForDataER16CfgNotifyForData,        0x212180, "OnNotifyForData")
TRACK2(_ZN9CfgServer14OnNotifyForAnyERK15CfgNotifyForAny,         0x2130e0, "OnNotifyForAny")
TRACK2(_ZN9CfgServer11OnWriteDataER12CfgWriteData,                0x225510, "OnWriteData")
TRACK2(_ZN9CfgServer11OnWriteViewERK12CfgWriteView,               0x2280b0, "OnWriteView")
TRACK2(_ZN9CfgServer15OnConnectClientERK16CfgConnectClient,      0x22a3c0, "OnConnectClient")
TRACK2(_ZN9CfgServer12OnUpdVersionERK10UpdVersion,               0x233bb0, "OnUpdVersion")

/* ── PostMessage(GMessage const*, astring const& target, bool) — the by-name post path (SendNotifySync
 * posts the CfgNotifySync here). FILTERED to Edit/Notify targets so it is low-volume + load-safe. Logs the
 * target queue name + the msg type (*(msg+4)&0x7fffffff) — reveals if a CfgNotifySync(0x1704E0) is ever
 * posted to .EditThreadNotify. */
void _ZN9CfgServer11PostMessageEPK8GMessageRK7astringb(void *thiz, void *msg, void *target, char b) {
    const char *t = sstr(target);
    unsigned mt = (msg && (uintptr_t)msg > 0x1000) ? (*((unsigned *)msg + 1) & 0x7fffffff) : 0;
    if (t && (strstr(t, "Edit") || strstr(t, "Notify") || (mt & 0xffff00) == 0x170400))
        lg("   >>PostMessage(target='%s', msgType=0x%x)%s\n", t, mt,
           (mt == 0x1704e0) ? "  <== CfgNotifySync!" : "");
    REAL(0x1860f0, void(*)(void*,void*,void*,char))(thiz, msg, target, b);
}

/* OnUpdNewState(UpdNewState&) — the injected-UpdNewState handler (INJECT_UPD/INJECT_REREAD path) */
void _ZN9CfgServer13OnUpdNewStateER11UpdNewState(void *thiz, void *msg) {
    int b = notif_flag(thiz);
    lg("   >>OnUpdNewState (SyncMap count before=%d)\n", b);
    REAL(0x18d260, void(*)(void*,void*))(thiz, msg);
    int a = notif_flag(thiz);
    lg("   <<OnUpdNewState (SyncMap count after=%d)%s\n", a, (a>b)?"  <== RAISED":"");
}

/* SendConnected() — called by OnUpdNewState; may broadcast the config-change sync-notify */
void _ZN9CfgServer13SendConnectedEv(void *thiz) {
    int b = notif_flag(thiz);
    REAL(0x18dc90, void(*)(void*))(thiz);
    int a = notif_flag(thiz);
    lg("   SendConnected  SyncMap count %d->%d%s\n", b, a, (a>b)?"  <== RAISED":"");
}

/* OnReconfigPermit(CfgReconfigPermit const&, bool) — 3 args */
void _ZN9CfgServer16OnReconfigPermitERK17CfgReconfigPermitb(void *thiz, void *msg, char x) {
    int b = notif_flag(thiz);
    REAL(0x231bb0, void(*)(void*,void*,char))(thiz, msg, x);
    int a = notif_flag(thiz);
    if (a > b) lg("*** SyncMap count %d->%d in OnReconfigPermit\n", b, a);
}

/* ReadConfigDataSet(bool, bool) -> void* (the reconfig triggered by INJECT_REREAD's UpdNewState) */
void *_ZN9CfgServer17ReadConfigDataSetEbb(void *thiz, char a2, char a3) {
    int b = notif_flag(thiz);
    void *r = REAL(0x229d50, void*(*)(void*,char,char))(thiz, a2, a3);
    int a = notif_flag(thiz);
    lg("   ReadConfigDataSet(reload=%d,a3=%d)  SyncMap count %d->%d%s\n", a2, a3, b, a, (a>b)?"  <== RAISED":"");
    return r;
}
