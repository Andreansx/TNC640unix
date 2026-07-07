# TNC640unix — project tracker

Lean project instructions + orientation. The full session-by-session frontier
log, the blocker-chain (#1–#6) narrative, the 335-object binary inventory, the
recompiled-function verification tables, recovered config values/recipes, and the
deprioritized Track A / x86_64 material now live in **`docs/PROGRESS-LOG.md`**
(split out 2026-07-06 to keep this file lean). Consult it for the history behind
any decision or the exact recipe for a past fix.

## Working preferences (user-set)
- **Commit messages:** DO NOT add "Claude-Session:" trailers or any trailing
  URL/session identifier — subject + body only, no Co-Authored-By. The git user
  is configured, so attribution is clear from `git log --format=fuller`.
- **Commit AND push autonomously** to `origin/main` without asking (this repo).

## ★ GROUND TRUTH + FRAMING (2026-06-24, user-set — read before writing "impossible")
1. **No license is involved.** The TNC640 PGM-Platz is the FREE Heidenhain
   download; it runs in **demo mode** as shipped — by design, and how essentially
   everyone uses it. There is **nothing to work around and no "licensing ceiling."**
   Where the SIK device still matters technically (e.g. `hessrv` reading a zeroed
   SIK device), the honest task is **reproducing the demo SIK *state* that ships
   inside the free image** — a state-reproduction engineering task.
2. **Frontiers, not walls.** Nearly every gate previously written up as the
   "documented ceiling / infeasible" was later CROSSED. Call them **frontiers** —
   large *engineering* problems, not impossibilities. Resource limits are config
   (the lima VM's allocated RAM), not laws — the whole appliance fits in 8 GB; the
   Mac has 32. Don't pre-declare things infeasible.
3. **`yeen` (x86_64 VirtualBox + VNC) is a reference fallback, NOT the deliverable.**
   Running the x86_64 guest in a hypervisor is trivial and proves nothing new. The
   goal is the **i386 control running natively translated on Apple Silicon (FEX +
   the heroscall emulator) → the real MMI as a Mac window (Track B).**

## ★ STRATEGIC FOCUS — Track B only, ARM64-native (2026-06-22, user-set)
The **sole** focus is **Track B**: run the i386 control natively on Apple Silicon
(ARM64) under FEX-Emu + the LD_PRELOAD heroscall emulator, reaching the real MMI
as a Mac window. **Do NOT pursue Option A** (x86-64 guest in a hypervisor) — it is
already done (docs 11 / README) and re-proving it does nothing for the
Apple-Silicon goal. The handwheel (TCP 19035) + JHIO (TCP 19009) protocol RE is
track-agnostic and still useful. If a request seems to point back at
x86-64/Option A, flag it first.

## Goal
Run HEIDENHAIN's TNC640 programming station (PGM-Platz Virtual, all-i386 control)
on Apple-Silicon ARM64 — natively via FEX + the heroscall emulator — to the real
MMI. Background + measured findings: `docs/` (start with `02-architecture.md`,
`15-apple-silicon.md`, `16-arm64-decompilation-and-translation.md`); full history
in `docs/PROGRESS-LOG.md`.

## Working environment (Apple Silicon M2 Max)
- ARM64 Linux VM: lima instance **`tnc`** (Ubuntu 26.04, vz). `limactl shell tnc -- <cmd>`.
- Host tools: Ghidra 12.1.2 + openjdk@21 (headless decompile), rizin, patchelf, lima.
  IDA idalib headless (`scratchpad/idalibvenv` + `work/re/scripts/idadecompile.py`)
  — cleaner than Ghidra on the exception-`.cold`-split binaries.
- VM tools: **FEX-Emu** (i386→ARM64, the main translator), qemu-i386,
  `i686-linux-gnu-gcc`, native gcc. FEX rootfs staged at **`/var/tmp/lr`**.
- Control extracted to `work/control/sysroot/` (binaries) + `work/target/rootfs/`
  (HeROS OS). Decompiler pipeline: `work/re/scripts/DecompileToFile.java` +
  `batch_decompile.sh`.
- **Recovered config values** (encfs key, OEM code numbers, root-access steps) are
  recorded in `docs/PROGRESS-LOG.md`, **not here** — keep them out of the
  always-loaded file.

## Current frontier (2026-07-06)
The FEX-native path (Track B) runs deep. Milestones reached: config #6 SOLVED;
IPO connects + passes `-k=NC`; the AppStartMP logo deadlock + the FEX-native
**constellation spawn**; a real **TNC640 GTK window (Guppy/HwViewer) rendered
FEX-native** and surfaced as a native **XQuartz Mac window** (no VNC); a **visible
softkey bar** (via the BARCOPY helper); the winmgr **render frontier CROSSED**
(winmgr creates its screen-layout windows, crash=0, via `readfix` + `PNAME=1` +
`SEM_INIT=0`/`SEM_FORCE_OK`).

**Latest state (2026-07-06, cont. — commit 7e764b6): the winmgr render-thread
SIGSEGV frontier turned out to be a NON-ISSUE (false positive); winmgr creates the
OEM screen cleanly.** Two tooling fixes exposed it: (a) `__cxa_throw` is a
**versioned** symbol (`__cxa_throw@@CXXABI_1.3`) — an unversioned LD_PRELOAD def
doesn't satisfy the versioned ref, so `throwcatch` caught 0 throws for ~10 sessions;
rebuilt with `-Wl,--version-script=emulator/throwcatch.map` it interposes. (b)
`WM_SEGVBT` was never in the run script's `sudo env` passthrough, so the diagnostic
preload was silently never applied — now fixed (+ a `WM_STRACE_TRACE` knob). With
versioned throwcatch loaded, the crash resolved to an uncaught
`Xml::Exception`/`SAXParseException` **"File not found"** from `libxmlreader.so` in
`WindowManager::ReadLayout`; an `openat` strace named the file:
`/mnt/sys/resource/tnc640layout1280_oemscr.xml = ENOENT`. **The `<screen>`-OEM
diagnostic layout was only staged to `$CFG`/SYSW, never to `/mnt/sys/resource`
(where `%SYS%` actually resolves — a persistent mount the run script does NOT
populate).** `sudo cp` it there and re-run → **winmgr creates all three screens incl.
`0x40001b "OEM"` (1280x1023) + `_JH_FOCUSPROXY` + the softkey-area strip, crash=0, no
throw** (`scratchpad/shots/oem_staged.png`). So the OEM-screen path is FINE. **DURABLE
LESSON: custom `%SYS%/resource/*` layouts must be `sudo cp`'d to `/mnt/sys/resource`,
else winmgr throws uncaught "File not found".**

**★ bar-blocker RE'd to the finest level (2026-07-06 cont.): the draw gate is now a
SINGLE missing event.** `Ev_receive(0x07011000)` is NOT the block (skmgr passes it).
New tools `WMGR_MSGDUMP` + winmgr `HandleMessage@0x29f00` decompile captured skmgr's
exact WM protocol (timer-driven, NOT GetAreaRect): `0x302c StartTimer`×3 / `0x3037
GetScreens` / `0x302d StopTimer`×3 / `0x301b OnRequest+WmSendEvent(a1[6]=0,dest,24)`.
With `INJECT_WMGR_ACK=0` (REAL winmgr) + `INJECT_SK_FLOW=1` (injected content) skmgr
COMPLETES the handshake (winmgr reads Q_WMGR 10×, sends 3× 208B GetScreens to skmgr's
0x313) — then BLOCKS polling 0x313 (`hc 0x0c`, an unhandled Q-peek stubbed to "empty")
+ `Sm_request(0x209)` + `Ev_receive(0x1000)`, **waiting for winmgr's 24B WmSendEvent
confirmation from `0x301b` that NEVER arrives** (winmgr q_sends only 0x313-GetScreens +
0x308-AppStartMaster; a1[6]=0 ⇒ no send). So skmgr's GUI thread never reads the 3rd
softkey msg (SkMgrActivate) on 0x314 (`Q_read 0x314` stuck at 2=Login+SetMenu) → 0 .bmx,
empty strip. INJECT_SK_ACTIVATE can't rescue it (its count gate needs skmgr's 0x028a0740
InfoResponses, which the bypass path produces 0 of). Configs EXHAUSTED (all blank):
inject-bypass, faithful-Guppy `barrun.sh`, SK_FLOW+SK_ACTIVATE, real-winmgr+injected.
**NEXT (single concrete point): make winmgr's `0x301b` confirmation reach skmgr's 0x313**
— decompile winmgr's `WmSendEvent` (IDA-inferred, no dynsym) for its queue resolution
from a1[6]=0 (likely the registered WmClient's queue) + whether skmgr registers (its
connect isn't 0x3001); then fix the emulator's queue resolution OR synthesize the 24B WM
event to 0x313 → skmgr reads the Activate → `BuildSoftkeyBar` → draws. Full chain, every
`INJECT_*`/env knob, and the exact run recipe are in `docs/PROGRESS-LOG.md`.

**★★ GLADE CASE BUG FOUND + FIXED + a CONFOUND REMOVED (2026-07-06 cont.): the
recent tmpfs runs were stuck BELOW the "19 .bmx" closest state — at a case-sensitivity
bug, not a downstream gate.** Running the default Guppy-drives constellation and reading
Guppy's Python traceback exposed it: `HwViewer.py:3325 jh.gtk.glade.XML('form/HWViewer.glade')`
→ `RuntimeError: could not create GladeXML object`. HEIDENHAIN's code requests
`form/HWViewer.glade` (upper "HW") but the file staged on the case-SENSITIVE VM tmpfs is
`HwViewer.glade` (lower "w") → exact-case lookup fails → HwViewer aborts BEFORE creating
any window → no softkey login. This **regressed when Python-tree staging moved off the
Mac's case-INSENSITIVE virtiofs (where it silently resolved) onto the VM-local
case-SENSITIVE tmpfs** — so the "19 .bmx" milestone PREDATES tmpfs, and recent ~10
sessions on tmpfs could not actually be reaching the downstream gates they pinned. FIX =
a glade case-alias step in `run_3proc_skmgr_guppy.sh` + `run_guppy_window.sh`
(symlink the code-cased name to the on-disk file). VERIFIED end-to-end via
run_guppy_window.sh (:99): glade-error=0, HwViewer now loads glade → builds window →
reaches `jh.softkey.Register` (was aborting at the glade). See [[project-guppy-glade-case-bug]].
**BUT the glade fix is NECESSARY-not-SUFFICIENT for the bar:** with it applied +
`WINMGR=1` at `:0.0`, Guppy's WM handshake now blocks UPSTREAM of the glade — winmgr
sends 16+3×208B GetScreens to Guppy's `WMQ00109` (0x31e) then Guppy polls 0x31e ~335k×
forever for the per-client 24B confirmation that never comes (SAME `0x301b`/a1[6]=0 gate
as skmgr's 0x313, now confirmed for Guppy). **This block is LAYOUT-INDEPENDENT** — both
`tnc640layout1280.xml` AND `...1024.xml` deadlock identically (the memory's "1024 →
Guppy login completes" does NOT reproduce on tmpfs). So the softkey-bar gate is UNCHANGED
(winmgr's 24B WM-confirmation), just now free of the glade confound. **The `P_name`
lead is TESTED-NEGATIVE:** new `GUPPY_PNAME=1` knob (default off; needed the sudo-env
passthrough fix to propagate) makes winmgr's `P_name(tid=265) -> "~/Guppy"` resolve
correctly (was `""`), but Guppy STILL spins 0x31e ~200k× — so winmgr's P_name(client)
call is diagnostic/naming, NOT the confirmation routing; the gate is genuinely the
`a1[6]=0` message field. NEXT (single concrete point): synthesize the 24B WM
confirmation to the client's `WMQ<tid>` queue (0x31e for Guppy / 0x313 for skmgr). **The
24B wire format is now DECODED** (from `scratchpad/winmgr_handlemsg.c`, `HandleMessage@0x29f00`
**case 0x301C/0x301D** — CORRECTION: NOT 0x301B, which does `FreeClientResources`+no send;
the ONLY 24B `WmSendEvent(a1[6],dest,24)` in HandleMessage is at case 0x301C/0x301D, line 860).
`dest` is a contiguous 24B stack struct: off0=`0x3045`(12357), off4=`a1[2]`, off8=`a1[1]`,
off12=`a1[7]`, off16=`0xffffffff`, off20=`1`; dest=`a1[6]`. **BUT a WMGR_MSGDUMP=1 capture
REFUTES this as Guppy's blocker:** Guppy's ACTUAL WM sequence to Q_WMGR is
`0x302c StartTimer(seq1, replyq=0x31e)` → `0x3001 Connect(seq2, replyq=0x109)` →
`0x3037 GetScreens(seq3, replyq=0x31e, a10=0)` — **NO 0x301C/0x301D**, so that 24B/0x3045
event is NOT what Guppy awaits. Winmgr replies 16 + 3×208B to 0x31e; Guppy reads all 4, then
polls 0x31e FOREVER and **never sends `0x302d StopTimer`**. Since the client's
`WaitForExpectedMessage` correlates replies by the msg+4 seq, and a `StartTimer(0x302c)` gets
NO immediate reply (it arms a timer that fires an event LATER), the live hypothesis is:
**Guppy waits on 0x31e for the StartTimer(0x302c) timer/completion event (correlated to seq 1)
that the emulator never delivers** — a timer→client-event gap, NOT the 24B confirmation.
**★★★ WM-TIMER FRONTIER CROSSED (2026-07-07, commit 1fe083e): the timer→client-event gap is
CONFIRMED + SERVED — Guppy now consumes the tick and PROCEEDS off the WM handshake.** RE'd
winmgr's `HandleMessage` case 0x302c (StartTimer) + the full mechanism: winmgr's 55ms
`WmWaitableTimer` (`FTimer::SignalEvery(55000)`→tm_evevery)→`TimerTick` posts a **WIRE-0x3061**
event to each armed client's WM queue; the client's `WmParseEvent` maps 0x3061→**parsed type
24**, `WmCheckTimerCallback` matches (`*parsed==24 && a3==a2[3]`)→the jh.gtk timeout callback
fires. Confirmed the emulator's one-shot `timers_fire` never re-arms winmgr's tm_evevery
cross-process, so nothing delivered it. **FIX = `HEROSCALL_INJECT_WMGR_TIMER` (heros_rtos.c):**
synthesize the wire-0x3061 tick (off0=0x3061, off4=serial kept contiguous with the client's
last-read `a1[10]`, off12=timerid), auto-discover the client's WM queue from its 0x3001/0x3037
reads, register the timer on its 0x302c send. **HANDSHAKE-GATED** (only after a GetScreens read
+ settle + drained queue — a mid-handshake tick routes a 0x3037 into WmParseEvent→
"WMGRErrUnexpected WINMGRQ_GETSCREENS"→SIGTRAP, the early crash before the gate). **DUAL-HOOK**
(fires from q_read poll AND ev_receive block + wait-cap; Guppy alternates, so a single hook
delivered only one tick then stalled), clamped to winmgr's faithful 55ms. VERIFIED (run_3proc,
WINMGR=1 INJECT_WMGR_TIMER=1 XDISPLAY=:0): crash=0, Guppy reads the ticks (serial 4,5), sends
its `0x3038` GetScreens follow-up, then proceeds — `Ev_send`, `Sm_ident "GuppyC"`,
`Q_send AppStartMaster(0x308)`, `Sm_request`, into the OEM/Python worker-thread setup. Guppy
does NOT send `0x302d StopTimer` (the hypothesised path); it proceeds directly.

**★★★★ OEM-THREAD-TERMINATE FRONTIER CROSSED (2026-07-07): the OEM thread self-terminate was NOT
an event-0x08 starvation — it was a WM-serial GAP that INJECT_WMGR_TIMER itself introduced; fixed by
`WM_SERIAL_FIX`, and Guppy now drives its full OEM interaction (no more `As_send 0x00800000`+`T_delete`).**
The "task 0x109 busy-polls `Ev_receive(0x08)` then `As_send(0x00800000)`+`T_delete`" was the SYMPTOM,
not the cause. Live trace (`g_pystdout`): after the injected tick, HwViewer.py's window setup calls
`WmGetCurrentScreen` → `WmSendRequestReply: Gap in event serial number sequence!` →
`WmGetCurrentScreen error: WMGRErrSync: Client - server communication out of sync` → the OEM thread
takes its alertable-terminate path (`As_send 0x00800000` self-signal, `As_read` caught, `T_delete`)
BEFORE `jh.softkey.Register`. **Root cause RE'd:** the WM client requires EVERY event on its WM event
queue (`WMQ<task>`, e.g. 0x31f) to carry a strictly-contiguous serial in off4 — `WmRecvEvent` /
`WmSendRequestReply` (libwinmgrlib 0x46d0 / 0x42b0) check `off4-1 == a1[10]` and advance a1[10], else
"Gap". winmgr assigns serials BLINDLY from a per-client counter (`WmClient::SendReply@0x1e650`,
`WmClient+56`, pre-increment) and does NOT read back the client's echoed serial (`WmRecvRequest@0x39fd0`
ignores it). So an INJECT_WMGR_TIMER tick inserted into the stream STEALS a serial (e.g. 4) that winmgr
then REUSES for its next real reply → `4-1=3 != a1[10]=4` → gap. **FIX = `HEROSCALL_WM_SERIAL_FIX`
(default = INJECT_WMGR_TIMER; heros_rtos.c q_send): the emulator is the SOLE serial authority for `WMQ*`
queues** — delivers winmgr's events UNCHANGED until the first tick (offset 0), then shifts every
subsequent winmgr event's off4 up by the running tick count (`q->wm_tick_offset`), and numbers the tick
itself `wm_last_serial+1` (`in_wm_tick`). Downstream-only + winmgr never sees the shift (shared per-queue
counter in /dev/shm, assigned under the queue lock so it's contiguous regardless of the winmgr/tick race).
VERIFIED (run_3proc, INJECT_WMGR_TIMER=1 WINMGR=1 XDISPLAY=:0): gap/WMGRErrSync/`As_send 0x800000`/
`T_delete` markers = **0** (were the terminate), 2 WM_SERIAL_FIX shifts applied, Guppy crash=0, and Guppy
now runs `PyJHKernel::Execute` → the FULL OEM interaction, driving winmgr all the way to OEM-screen creation.

**NEXT GATE (now re-exposed, separate + documented): the winmgr render-thread SIGSEGV during OEM-screen
creation.** With Guppy driving its full OEM window registration (`GuppyRegisterWindow@0xc7920`, WINMGR
path), winmgr reaches OEM-screen bring-up and a winmgr sub-thread does `P_signal(0xffffffff,0x02000000)
→ P_name(-1)->"~/winmgr" → T_name(-1) → SIGSEGV` (`libheros_sigfaterr: Thread ~/winmgr.~/winmgr
received terminating signal 11`) — the EXACT documented crash (root-caused earlier to an uncaught
`throw const char*`/PKc from a LINKED lib during OEM-screen creation → EvtExceptionShell retry desyncs
the FModule eval-context). This is NOT a WM_SERIAL_FIX regression (winmgr didn't crash pre-fix because
Guppy terminated first); the fix simply advanced Guppy past the serial gap into the interaction that
triggers it. NEXT: capture the winmgr fault EIP/thrown-string via a FEX-respecting mechanism (intercept
the guest rt_sigaction/`__cxa_throw` inside heros_rtos, or suppress libheros_sigfaterr so FEX's own crash
dump prints the guest RIP) + find WHICH linked-lib call throws during OEM-screen bring-up + provide the
emulator data it needs (candidate: the no-op `P_signal(0x2b)` stub preceding the throw). Run:
`emulator/run_3proc_skmgr_guppy.sh` with `INJECT_WMGR_TIMER=1 WINMGR=1 XDISPLAY=:0` (WM_SERIAL_FIX auto-on).**

## Key run scripts (`emulator/`)
- `run_3proc_skmgr_guppy.sh` — the main softkey-bar constellation harness
  (ConfigServer + skmgr + Guppy/HwViewer + winmgr).
- `run_guppy_window.sh` — the HwViewer GTK window FEX-native (`GUPPY_C=HwSetup`);
  `guppy_xquartz_mac.sh` — surface it as a native Mac XQuartz window.
- `run_fred.sh` — operator-MMI scout (Fred/simulo; `WINMGR`/`SKMGR`/`GRAPHICS` knobs).
- `run_2proc_hrmmi.sh` (handwheel MMI), `run_appstart_fex.sh` (constellation
  launcher), `run_2proc_cfgfix.sh` / `run_2proc_fex.sh` (config #6, 2-proc connect).
- `stage_guppy_pytree.sh` — **RUN ON THE MAC once**: stages the Guppy Python tree
  via SSH/rsync (NOT virtiofs — virtiofs silently corrupts file content under load).

## Durable lessons / tooling caveats (carry forward)
- **Rosetta is x86-64-only** → it cannot translate this i386 control.
- Native ARM64 `objdump` can't disassemble i386 → use `i686-linux-gnu-objdump`.
- **FEX leaks `/etc` *writes* to the real lima guest.** Always run `/etc`-writing
  servers CONTAINED in a mount-ns (bind rootfs `/etc` over `/etc`), or an as-root
  server rewrites guest `/etc/passwd` and SSH breaks. Recovery = offline disk repair
  (helper VM + `losetup` the raw disk + restore from `/etc/passwd-`); recipe in
  `docs/PROGRESS-LOG.md`.
- The **lima Mac-mount (virtiofs) is read-only from the VM AND silently corrupts
  file content under load** (correct size, blank/garbage bytes) → stage via SSH/
  rsync to VM-local disk (`/var/tmp`), verify by md5. Build in VM `/tmp`, `limactl
  copy` back; patchelf runs host-side.
- A recompile candidate must be **EXPORTED in `.dynsym`** to be the truth oracle
  (local symbols aren't dynamically linkable).
- x87 `fistp`/`fisttp` of 80-bit intermediates near integer boundaries isn't
  bit-reproducible on ARM SSE — excludes a few FP fns from the byte-identical bar.
- **Cycle libs are function-pointer-table architectures** — most "exports" are
  runtime-registered forwarder thunks (`jmp *GOT`), not reimplementable.
- Never pipe FEX through `| head` under `timeout strace` — FEX detaches and
  survives the dead tracer → deadlock. Use `>file` + `pkill -KILL -x FEXInterpreter`.
- **Don't `limactl restart` to "fix" a flaky constellation run** — it destroys warm
  VM state and trades workable-flaky for stuck.

## Reproduce / verify
- heroscall emulator on ARM64: `emulator/run_2proc_fex.sh` (FEX) or
  `run_2proc_arm64.sh` (qemu-i386). Docs: `docs/17-heroscall-emulator.md`.
- Recompile proof: `recomp/build_and_verify.sh`. **278+ functions** verified
  byte-identical or behaviorally-equivalent vs the genuine i386 code (full tables +
  method in `docs/PROGRESS-LOG.md`; the set is NOT exhausted).
