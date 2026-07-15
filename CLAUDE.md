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

## Current frontier (2026-07-15) — REAL-DRIVER bar: GATE 1 CROSSED; GATE-2 PROM CRASH CROSSED (real fix); new blocker = Fred SIGSEGV
**★★★★★★★★★ GATE-2 PROM CRASH CROSSED — REAL ROOT-CAUSE FIX, no inject (2026-07-15).** promview's
`terminate: PciHardware::Exception` (the provable Gate-2 blocker that stalled the bar for weeks) was NOT
missing hardware — it was a **cross-UID shared-memory permission bug**. Pinned un-fakeably with a new
`__cxa_throw` interceptor (`emulator/cxathrow.c`, knob `CXATHROW=1`): throw type `PciHardware::Exception`,
code 3, **site = libhwaccess.so +0x208f = `IpoSharedMemory::GetMemoryPointer`'s `m_attach==0` path** (the
earlier 6d0452c triage instrumented m_ident/m_attach but the SUCCESS/openat-fail paths were LOG-suppressed).
Root cause via `HEROSCALL_REGLOG=1` (new): `M_attach "TR_en"/"IPO_SHARED_MEMORY" -> openat(/dev/shm/heros_reg_*)
FAILED rc=-13 (EACCES)`. The region files are created **0600 root:root** by root procs (ConfigServer/IPO), but
**promview drops to a non-root UID** (opens the ctl file 0600 while still root at init, then the setuid — invisible
to the execve-only strace filter — precedes the region attaches), so its `openat` is denied → `m_attach` returns
0 → `GetMemoryPointer`/`GetVirtPciBaseSingle` throw. **FIX (`emulator/heros_rtos.c` `reg_attach`+`ctl_init`):
create /dev/shm region + ctl files 0666 + explicit `fchmod(0666)` — models the genuine "the WHOLE control
constellation shares this memory" semantics.** VERIFIED un-fakeably: region files now `rw-rw-rw-`, **EACCES=0,
PciHardware throws=0, promview STAYS ALIVE** (pid Sl), reaches `OnCfgClientIsConnected():99:`, reads PLC config,
serves QProMViewer. **NEW downstream blocker PINNED to a Fred USE-AFTER-FREE (2026-07-15 cont.):** with prom
alive, Fred (Ed/mmi) SIGSEGVs in `libbackend.so` **`FThread::EvalContextModule` (libbackend+0x28bf2 `call
*0x18(%edx)`**, reached `FrameThread::MainContext`←`CreateContext`←`CreateMainContext`←`FThread::Run`; the
`+0x27bf2/EvalContextInQueue` in the earlier note was the NEXT call in the same MainContext — misattributed).
Root cause pinned un-fakeably (4x repro A–D, ground-truth via the crash handler now dumping GP regs +
`this`→`m_0x4c`(registry)/`m_0x60`(module-array)): the re-eval path (count=2,idx=0) reads
`P = this->m_0x60[0]` — a FThread **context object that is a DANGLING pointer**: sometimes UNMAPPED (fault
reading `*P` at +0x28bef), sometimes readable-but-garbage-vtable (`*P`=0x0 / 0x656d6186 / 0x881c04fe, a
DIFFERENT garbage every run). **Forks RULED OUT:** (a) emulator message misroute/misserialize — the pre-crash
msg `QR[307]"QEvtServer" size=373 tag=0x00320221` is **Fred-INTERNAL** (t10e→t10d, both Fred) and passed
q_send **verbatim** (no mutation for queue 307), so a yeen byte-diff is moot by construction; (b) loader/reloc —
all `FrameModuleAlloc`/`FrameModule` vtables + `R_386_RELATIVE` slots valid, and ONLY Fred faults; (c)
parallelism race — pinning to 1 CPU (`HEROS_PIN_CPU=0`) still crashes deterministically. **UAF CONFIRMED** via
`emulator/fredfree.c` (`FREDFREE=1`, process-scoped no-op `free`/`delete` in Ed/mmi only): loading it in Fred
MOVED the crash PAST EvalContextModule (to libbackend+0x31470) — a use-before-init could not be moved by a
free-noop, so arr[0]'s object is genuinely freed-then-reused. **Next: find the erroneous FREE site** (a
free-logger ring in Fred + dump-on-crash → the caller that frees the context object; then decide emulator
event-ordering vs genuine Fred double-free/temp-lifetime). NOT yet a bar draw; Fred still dies before
activation. See [[project-gate2-fred-eval-context-uaf]], [[project-gate2-prom-crash-fixed-uid-perms]].
Diagnostics (default OFF, keep): `CXATHROW=1` (throw tracer), `HEROSCALL_REGLOG=1` (region-op log),
`HEROS_PIN_CPU=N` (1-CPU serialize test), `FREDFREE=1` (Fred-scoped no-op-free UAF probe); the crash handler's
reg+FThread-walk dump is always-on under `HEROSCALL_BTRACE=1`.

**★★★★★★★★ GATE 1 CROSSED (commit 353856c).** On the real-driver bar3 path Fred blocked forever on its
`CfgWriteNew(0x170461)` — no `CfgWriteDone`. Root-caused un-fakeably via `emulator/cfg461probe.c` (LD_PRELOAD
tracer over libConfigSystem.so): `CfgServer::OnWriteNew`→`CheckNotification` EARLY-OUTs (defers, no reply)
while `this+232`!=0 = the SyncMap (`std::map<astring,Access>@this+212`) node-count of subscribers awaiting a
`CfgNotifyDone`. Stuck at 1 with subscriber **`.EditThreadNotify`** (ConfigServer's OWN internal EditThread,
queue "EditThreadNotify"=0x302). **Emulator bug:** every message ConfigServer sent to `.EditThreadNotify`
went to the "" BLACK HOLE (0x30b) — `q_basename` strips the LEADING dot →`""`→`q_find_slot("")` matches the
empty-named queue (id!=0) so all `!id` guards skipped. FIX = **`HEROS_QIDENT_DOTLEAD` (default ON)**: `q_ident`
resolves a leading-dot target `.X` → the real queue `X` (disjoint from qident_notify's compound case + the
`""` CFG_REPLY_ROUTE path). VERIFIED un-fakeable: EditThread READS 0x302 → `OnNotify@0x2719d0` sends
CfgNotifyDone → **OnNotifyDone before=1→after=0 (SyncMap DRAINED)** → OnWriteNew(notif=0) → CheckNotification→0
→ **WriteFinish→0**. Fred then drives the FULL real softkey conversation: SkMgrLogin(0x028a0120)→resp(0x028a0140)
→SetMenu(0x028a0981)→real .bmx loads(0x028a0421)→MID_MAIN+24×per-key. NO injects. See
[[project-config-gate-0x170461-editthread-syncmap]].
**GATE 2 (the bar DRAW) — live.** Screen topology mapped (winmgr GetScreens + tnc640layout1280.xml): 3 screens
**Nc/Machine(desktopId=0, ACTIVE — prom's boot splash, "PLC not ready→startup picture visible"), Ed/Edit(desktopId=1
= FRED's), OEM(2)**. Bar draws when skmgr gets `SkMgrActivate(0x028a0200)` (`SkMgrGMsgController::OnActivation@0x5a5a0`
sets state=3→GData::Notify→`SkMgrFrame::OnActivation@0x42170` draws), SENT by Fred's `SkMgrCtrlInterfaceImpl::Activate`
(libSkMgrCtrl 0xc5d0) on view-activation. Drove a real WM screen-foreground to Edit(1) via `INJECT_WMGR_ACTIVATE`+
`WMACT_SCREEN=1` (byte-exact `WmSelectForegroundMsg` = the genuine screen-switch, NOT INJECT_SK_ACTIVATE): winmgr
foregrounded Edit (openbox "desktop 1" popup), **Fred RECEIVED the SCREENCHANGED(0x3067)** (never did before) and
reacted (19× 0x3038 GetScreens) — but did NOT activate its view / send SkMgrActivate. So a raw SCREENCHANGED is
necessary-but-NOT-sufficient. **RE'd the FULL chain (2026-07-12):** the bar draws on Fred's real
`SkMgrActivate(0x028a0200)`, sent only after `FControl` activation-state (member85) reaches 3, set by a
**`PromActivateNotifyMsg(0x404703E0)`**. On a real boot prom sends it; promview.elf CRASHES at init
(`terminate: PciHardware::Exception`, libhwaccess.so; a_appstart 1544) before its message loop, so it never does.
**★★★★★★★ PATH B — prom-less activation — CROSSED the prom-crash blocker (2026-07-12).** Route around dead prom
via Fred's OWN standalone code (the genuine `FControl::ActivateMyApp@0x89450` self-synth branch, member136==-1):
DELIVER the byte-exact default `PromActivateNotifyMsg` — 12-byte wire `[0x404703E0][screen=0][state=0]` from the
genuine libGMessageGui serializer (scratchpad/build_promwire.c) — to Fred's OWN FControl queue **ProgEditQueue(id
332)** (the queue Fred advertised to prom in its QProMRequest registration: payload "Ed/mmi.ProgEditQueue").
`FControl::DispatchMessage@0x89b10` routes it → `OnProMActivateNotify@0x891a0` → **member85=3** + RequestClientFocus
(member136 only gates a harmless ack to dead prom). Knob **`HEROSCALL_INJECT_PROM_ACTIVATE=1`** (heros_rtos.c,
default off) + WMACT editor-foreground. **VERIFIED un-fakeably (hst trace):** wire delivered (`QS [332]ProgEditQueue
size=12 tag=404703e0 ->t10e`) → Fred read editor softkey-menu config → **Fred sent its OWN real
`SkMgrActivate(0x028a0200)` to skmgr** (`QS [31a]Q_SkMgr tag=028a0200 sndr=t10e`) → skmgr `SkMgrFrame::OnActivation
@0x42170` queried the HSoftKeyArea geometry (0x3003/0x300c) + **loaded the REAL .bmx softkey bitmaps** (end.bmx,
copy_paste.bmx). NO fake SkMgrActivate. See [[project-gate2-pathb-promactivate-wire]].
**★ LAST-MILE BLOCKER — the bar does NOT yet draw pixels; provable + un-fakeably measured (2026-07-12).**
[SUPERSEDED 2026-07-15: this paragraph's "ROOT CAUSE = the SAME prom crash" is now CROSSED by the real 0666
fix at the top of this section — prom stays alive with NO inject. The PATH-B inject knobs below are a scouting
fallback, not the path. The current blocker is Fred's `FThread::EvalContextInQueue` SIGSEGV, downstream of the
now-live prom.] [CORRECTS an earlier overstatement: the map gate is NOT crossed. The RE-MAP only RESIZED a child
window; it did not MAP the area.] After the real PATH-B activation the editor softkey area `ScreenEDIT_HorizontalManager`
(0x800004) stays **IsUnMapped** and its DefaultView (0x800006) **IsUnviewable** — 40/40 `xwininfo` samples across a
full REMAP-HOLD run, `_NET_CURRENT_DESKTOP`=0 throughout (scratchpad/bardiag.sh). The RE-MAP
(`HEROSCALL_PROM_ACT_REMAP`, re-post `WmSelectForegroundMsg(editor)`) only RESIZES the DefaultView geometry
(1x1→1280x88 via `WmWindowDesc::Resync@0x36c00`→`OnWindowUpdate`, the handle-EXISTS geometry-update branch); it does
NOT map. `WmScreen::Map@0x2e590`→`Resync` only CREATES+maps (`ReportMapped`) a desc when handle==null AND the
visible flag (desc+20) is set — and that flag is set by a client SHOW that **skmgr never issues**: after activation
skmgr QUERIES the softkey-area geometry (0x3003/0x300c) then SPINS on the screen-state poll **0x3038 (186×)** waiting
for its screen (Edit) to become the genuine foreground. It never does — the boot Machine screen ("PLC not ready"
splash) keeps winning (curDesk stays 0; the WMACT `WmSelectForegroundMsg` foregrounds Edit in winmgr only
transiently, openbox's current desktop doesn't follow). **ROOT CAUSE = the SAME prom crash**: promview.elf
(PciHardware::Exception at init) never runs its loop, so nothing ARBITRATES the persistent screen-foreground that
would let skmgr's poll proceed to the show. PATH B routed the ACTIVATION around prom; the persistent FOREGROUND
arbitration is a SECOND prom responsibility not yet routed around. **Faithful next step** (NOT a synthesized show —
that's a fake): make Edit the persistent foreground so skmgr's OWN 0x3038 poll proceeds to its OWN show → visible
flag set → Resync maps the area. Needs either the promview PciHardware crash resolved OR a persistent genuine
`WmSelectForegroundMsg` that wins the arbitration, VERIFIED by skmgr advancing past its 0x3038 poll to a
window-show. (Dropping prom — bar4 — was a REGRESSION: Fred stalls at its QProMRequest registration when prom's
queues don't exist, so bar3 with the crashed-but-present prom is required.) See
[[project-gate2-pathb-promactivate-wire]]. Diagnostic repro (map-state, no pixel): `HEROSCALL_INJECT_PROM_ACTIVATE=1
HEROSCALL_PROM_ACT_REMAP_HOLD=250 HEROSCALL_INJECT_WMGR_ACTIVATE=1 HEROSCALL_WMACT_SELECT=1 HEROSCALL_WMACT_SCREEN=1
HEROSCALL_WMACT_ONCE=1 HEROSCALL_WMACT_DELAY=150 HEROSCALL_PROM_ACTIVATE_DELAY=15 HEROSCALL_PROM_ACT_REMAP_DELAY=20
APPSTART_BATCH_NAME=TNC640heros_bar3.txt APPSTART_TIMEOUT=450 bash emulator/run_appstart_fex.sh` + scratchpad/bardiag.sh
→ area IsUnMapped, dv IsUnviewable (un-fakeable).

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
