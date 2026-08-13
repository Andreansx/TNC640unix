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

## Current frontier (2026-08-13) — ★★★ THE WHOLE BOOT SEQUENCE NOW RUNS: startup.elf drives FipsMain Init→ConnectServer→WaitHwInit→CheckHwSetup→PreInitNc and its own CallProcedure("StartNc") makes AppStart load the NC channel. Next = Nc/IPO's SIGSEGV in GMessage::IsValid + the servers nobody is running (Q_SQL, DNC, DlgServer)
**★★★★★★ bar23 RESULT (measured, crash-free except where noted).** startup.elf's own log:
```
FipsMain  Init → ConnectServer → WaitHwInit → CheckHwSetup → PreInitNc
FipsUI    IdleUI → HwInitRunning → IdleUI → IdleUInc → WaitNcSubsys → WaitNcSubsysInterrupt
FipsNc    Init → StartSubsysNc
FipsEvtServer  ConnectEvtServer → EvtReset
```
* **`WaitHwInit` → `CheckHwSetup` means the HW-server run-up trigger FIRED** — hwserver reached
  `HWSRunUpState Idle(7)` (`GetConfigData → InitDevs → Operate → Release control → Idle`) and
  `FipsIfHws::AddTriggerRunUp`'s trigger came back, exactly as the RE predicted.
* **`FipsNc::StartSubsysNc` calls `FipsIfAppStart::CallProcedure("StartNc")` and AppStart REALLY LOADS
  STAGE TWO**: `ipo_progstation.elf`, `plc.elf`, `PlcDaemon.elf`, `monitoring.elf` spawn only after
  startup.elf asks — the genuine, control-driven boot order, for the first time.
* Barriers behave: `SV "NcSI" released 2` (startup+CM) then `SV "NcC" released 4` (the stage-2 four).
  `Nc/startup`, `Nc/CM`, `Nc/MON`, `Nc/PlcDaemon` all reach INITIALIZED.

**★ bar24 (Server subsystem filled in: + SqlServer `-g`, dnc `-i=Nc -s=Sim`, DialogServer,
SharedMemServer — all shipped options): 16 processes spawn, and `Server/{SQL,DNC,smserver,hwserver}`
+ `NcS/{startup,CM}` + `Nc/{MON,PlcDaemon}` all reach INITIALIZED.** `Q_SQL` is now a REAL queue
(`QC "Q_SQL" id=348 owner=t113 flags=1000003 notify=01000000`) instead of an auto-created sink, and
plc's requests reach the server (`notify=01000000->t113`). Remaining, in priority order:
* **TWO processes fault, and they are different failures** (resolve each dump against its OWN maps —
  the two dumps interleave in one stderr):
  `line 4031 → SqlServer.elf sig=6` (glibc **`free(): invalid pointer`** → abort, moments after
  `RUNUP_COMPLETE`) and `line 21398 → ipo_progstation.elf sig=11`.
* **That abort is why the SQL server never answers**: it creates `Q_SQL`, reads exactly ONE request
  and dies, so plc's requests go unanswered. **Prime suspect, now fixed:** `Q_read`'s too-big check
  was `if (maxsize && full > maxsize)`, so a caller passing `maxsize=0` skipped it and the `memcpy`
  wrote a whole message into a zero-capacity buffer — a guest heap smash that surfaces later exactly
  as `free(): invalid pointer`. bar25 tests it (and logs the first `maxsize=0` read if one happens).
* `Nc/IPO`'s SIGSEGV is a separate item — in bar23 it landed in `GMessage::IsValid`
  (`libgmsglib.so+0x24020`, `+0x1a`, reading `[reg+0x44]`).
* `tm_check` (heroscall **0x31**) is unimplemented — the new first-use reporter caught it in ipo.

**★ THE TWO bar23 BLOCKERS (both concrete):**
1. **`Nc/IPO` SIGSEGVs in `GMessage::IsValid(GmIsValid_)`** — `libgmsglib.so+0x24020`, fault at `+0x1a`
   reading `[reg+0x44]` of a null/garbage object (`libheros_sigfaterr: Thread Nc/IPO.Nc/IPO received
   terminating signal 11`).
2. **Nobody serves `Q_SQL`.** The traffic immediately before that fault is `Nc/plc` firing temp
   mailslot names (`DB00000133N371`, `N372`, …) at `Q_SQL` — a queue the emulator auto-created as a
   sink. That is also the origin of the earlier 1.7M-`Q_ident` treadmill: an unanswered request loop
   that mints a fresh temp mailslot per attempt. The shipped `Server` subsystem is **not just
   hwserver** — it is observer, hwserver, cfgserver, **DNC**, **SQL**, flserver, HPServer, SIFCOM1/2,
   HlpSrv, **DlgServer**, cast, **smserver**, tasksrv, workset, and startup.elf's own context declares
   `DncInQueue`, `Q_DLGSERVER` and `QOsciCtrl` as out-queues. `emulator/TNC640heros_bar24.txt` adds
   SqlServer, dnc, DialogServer and SharedMemServer to the Server subsystem.

## Prior frontier (2026-08-13, earlier) — startup.elf was in **FailInit** all along ("No HwViewer instance specified on command line"); plus five real emulator bugs fixed
**★★★★★ THE HEADLINE.** Once startup.elf was given `-L=<path>` (without it `EvtMgr::WriteVerbosePrint`
@0x4a810 writes nothing — it needs `evtMgr+52`, which only `SwitchToCustomLogger`/`-L` sets, AND a
non-NULL `FILE*`; `-v` alone only sets the level), its own log answered in 40 ms what weeks of runs
could not:
```
|ACTN|FipsModule|Initializing Fips....      |TRNS|FipsMain|State Init entered.
|ACTN|FipsMain|Read configuration data.     |INFO|FipsMain| systemType:=WinSimulation simMode:=FullSimul dongleType:=noSIK
|ERRO|FipsMain|No HwViewer instance specified on command line.
|TRNS|FipsMain|State FailInit entered.
```
**startup.elf has been sitting in FailInit in EVERY bar run since bar5** — never blocked on a peer,
never waiting for hardware. It read its config, saw a HwViewer view configured
(`hwViewerRef.key="HwViewer"`), had no procedure name to launch it with, and refused to initialise.
Everything downstream (the ChM `StartupCycle` handshake, `FipsUI::EnterPowerInterrupt`,
`HideStartupPicture`, the bar) is gated behind a state machine that never left `Init`. The shipped
batch (`TNC640heros.txt:411`) launches it as
`-S=functional_safety -s=StartNc -H=LoadPython -I=LoadPython3 -M=%DIAGNOSIS%` — all four documented in
its own `-h` text. `emulator/TNC640heros_bar22.txt` uses that genuine line; `run_appstart_fex.sh` now
exports `DIAGNOSIS=/mnt/diagnosis` + `FIELDBUS=/mnt/fieldbus` like the genuine launcher
(`sysroot/application:119`).
**Also now genuine (`bar21`/`bar22`): the TWO-STAGE NC load.** The shipped batch splits
`NcS`(startup+CM, loaded up front) from `Nc`(IPO/plc/PlcDaemon/MON, `procedure:="StartNc"`), and
`FipsNc::StartNcSubsys`@0x50500 calls `FipsIfAppStart::CallProcedure("StartNc")` to load stage two.
Every earlier bar batch collapsed both into one subsystem, inverting the dependency. Verified: bar21
spawns exactly the 8 stage-1 processes and holds IPO/plc/PlcDaemon/MON back.

**★★★ THE SESSION'S EMULATOR FIXES — each measured, each reproduced in isolation, none an inject.**

**(1) `syscall()` returned the KERNEL ABI instead of glibc's — a coin-flip abort in any guest process.**
`heros_rtos.c` interposes libc's `syscall()` (heroscall = `syscall(222,…)`) and passed everything else
through a bare `int $0x80`, returning `-errno` and never setting `errno`. abseil
(`libabsl_synchronization`, loaded by hwserver) does `err = syscall(SYS_futex,…); if (err) return -errno;`
and treats anything but `-EINTR/-EWOULDBLOCK/-ETIMEDOUT` as `ABSL_RAW_LOG(FATAL)`. An ordinary `EAGAIN`
came back non-zero with a STALE errno (17 = EEXIST) → `[waiter.cc : 102] RAW: Futex operation failed with
error -17` → **SIGABRT in hwserver mid-DetectSik**, so the Nc subsystem never loaded at all. FIX: glibc ABI
(`-1` + `errno`) **plus** a 6-arg `raw6()` (musl's `__syscall6`; absl passes `FUTEX_BITSET_MATCH_ANY` as
arg6). Verified against real glibc as oracle under FEX (`scratchpad/syscall_abi_test.c`, `raw6_test.c`):
glibc `-1/EAGAIN` = patched; old passthrough `-11/errno=17` = the exact `-17` absl printed.
**(2) The semaphore UNITS parameter was ignored — AppStart's per-subsystem barrier freed exactly ONE
process.** `FProcess::SynchronizeTransition@libbackend+0x25880` reports a state to AppStartMaster then
PARKS on a named semaphore `<subsystem><state caption>` (`NcC`/`NcI`/`NcT`), retrying on a 100 s timeout
forever. AppStart opens it (`AppStartSemaphores.cpp`) by releasing **as many units as the subsystem has
processes** (`mov 0x28(%edi),%edx` = `SubsystemSemaphores.units`). The emulator released a hard-coded 1.
ABI from `libheros.so.1.8.6.2` (`sm_release@0xc940`, `sm_request@0xca30`): **`p[0]=id, p[1]=UNITS,
p[2]=timeout`**. Measured after the fix: `SV t106 sem=0x222 "NcC" released 6 -> count=6` and **all six Nc
processes proceed** (before: only `Nc/PlcDaemon`, the recurring bar7/bar9/bar13 signature). Semaphores are
now in the hst trace (`SC/SI/SW/SO/SV`) — they were the one startup primitive it never showed, which is why
a parked process looked like a process that had gone quiet.
**(3) The config connect-ACK dedup swallowed a legitimate RE-connect.** `inject_connect_ack` de-duplicates
per reply queue; hwserver's run-up runs **twice** (`DetectMainboard → … → Terminate → DetectMainboard`,
because the config asks for simulation mode) and its `CfgSrvMgr` does `Connect → … → Disconnect → Connect`.
The second `CfgConnectClient` was silently dropped → `CfgSrvMgr` sat in *State Connect entered.* for the
whole run. FIX: `CfgDisconnectClient` (wire tag **0x00170120**, same leading GMsgString client id) clears
the dedup entry. New trace lines `CA`/`CD`.
Also fixed: the crash handler re-entered itself **3541×** (guest handlers use `SA_NODEFER`, a garbage EBP in
the frame walk faulted inside the dump) — now re-entrancy-guarded and every address probed with `rd_ok()`
(`write(/dev/null,p,4)`); and the backtick landmine in `run_appstart_fex.sh`'s UNQUOTED `NSCMD` heredoc.

**STATE NOW (bar16/bar17, crash=0, FAULT=0, PciHardware=0):** 13 procs; `winmgr skmgr prom evtserver Ed/mmi
Server/hwserver` + `Nc/{PlcDaemon,MON,CM}` report **INITIALIZED**; `Nc/{IPO,plc,startup}` are **working, not
blocked** (the last wire traffic at the 700 s cap is `Nc/plc` reading `CH_NC`/`CH_SIM`, KERNEL/PRODUCT
versions and the axis table `X1 Y1 Z1 A1 B1 C1 U1 V1 W1 C2 …` from ConfigServer).

**★ THE NEXT LINK, already RE'd (idalib over startup.elf + hwserver.elf):** `FipsMain::EnterWaitHwInit`
(startup.elf 0x37090) registers HW-server run-up triggers — `FipsIfHws::AddTriggerRunUp@0x8a3c0` →
`HwsMailslotQueue::AddTrigger(ident "state", HWSRunUpState N)` for **N = 7 Idle / 14 RunUpFailed /
8 CritFwUpdateRequested / 10 CritFwUpdateComplete / 11 CritFwUpdateCompleteReboot** — then WAITS. The enum
is pinned from `HWSMain::Init@0x1ce010`: 1 Init, 2 DetectMainboard, 3 DetectSik, 4 GetConfigData, 5 InitDevs,
6 Operate, **7 Idle**, 8..11 CritFwUpdate*, 12 Terminate, 13 TerminationComplete, 14 RunUpFailed,
15 RunUpAborted, 16 SyncBreakpointMain, 17 SyncFramework. So **the gate is hwserver reaching Idle(7)** and
firing that trigger; then `FipsMain::LeaveWaitHwInit` → `ConfirmHwSetup` → `FipsUI::EnterPowerInterrupt`
(startup.elf 0x6d8c0, the ONLY caller of `FipsIfProM::HideStartupPicture`) → `HideStartupPicture 0x404705C0`
→ prom's `OnScreenChanged` guard drops → activation → the bar. Downstream checkpoints all still 0.
Full RE: `docs/re/appstart-subsystem-semaphore-barrier-re.txt`, `docs/re/gate2-prom-startup-picture-activation-re.txt`.
Diagnostics: startup.elf takes its own documented **`-v`** (Verbose) / `-V` (trace all messages) — used in
`emulator/TNC640heros_bar16.txt`; its `EvtMgr::FVerbosePrint` output is stdout-buffered, so prefer the wire
trace or try its documented `-L=<path>` custom logger.

## Prior frontier (2026-07-29) — ★ GATE CROSSED: Nc loads AFTER Server, ipo_progstation spawns, and hwserver SERVES GetIoRange
**★★★★★★★★★★★★★★ THE CROSSING (2026-07-29). 13 procs, crash=0, PciHardware throw=0.** A bar13 run
(`TNC640heros_bar13.txt` + `HEU_GRANT=1`) reaches the session's un-fakeable exit: **every process of the
Server subsystem reports INITIALIZED** (`Server:Server/hwserver` = **2** FmProcessState), so AppStart
dispatches the NEXT batch message and the **Nc subsystem loads AFTER Server** — the faithful genuine
ordering. `[rtos] SELF … self_pname="Nc/IPO"` = **ipo_progstation SPAWNS**, and on the **public
QHWServer (30c)**: `QS sndr=t123 [Nc/IPO:GetIoRange]` → `QR (rdr=t112)` (hwserver's own main task) →
**`QS [38a]"HwsM00000123N002" size=152 tag=008404c1 sndr=t112` = a POPULATED data reply** (vs the old
72-byte FAILED). **ipo then ADVANCES**, walking the HW tree (node/spi, profinet, cameraFlash, ports,
byPrjWriter) with hwserver answering each. 13 processes up: AppStart, winmgr, skmgr, prom, evtserver,
Ed/mmi, Server/hwserver, Nc/{IPO,plc,PlcDaemon,MON,CM,startup}.

**How it was cracked — three findings, in order.**
**(1) The "0x01019007 Monitor gate" was a phantom.** That `Ev_receive` on t106 is AppStartMaster's **idle
FThread dispatcher wait**; the trace shows it entered/left dozens of times per healthy boot. No subsystem
"satisfies" it; there is no 6-subsystem limit; nothing about AppStart is Server-specific. **The REAL rule
(idalib over AppStartMP.elf + libbackend.so):** the batch is fed **one message at a time** —
`Monitor::OnMessage(FmProcessState)@0x3c400` emits `FmAppStartAction(0)` when the LAST process of the
current subsystem reports **state 3 = INITIALIZED**, and `Procedures::OnMessage(FmAppStartAction)@0x4b420`
→ `DispatchMessageFromProcedure@0x48c90` dispatches the next `FmLoadSubsystem`. Children report via
`FProcess::SynchronizeTransition@libbackend+0x25880` (wire **tag 0x40c803e0** → the AppStartMaster queue).
Diagnostic: **count `tag=40c803e0` per process — 2 = healthy, 1 = stuck.**
**(2) hwserver was stuck at `RunUpFailed`** because `DetectMainboard`'s first call,
`GetDataSys("initMode","(value)")`, was DENIED. Chain, each hop measured: the reply's code 14 predicted a
thrown exception with code 3 (`Inspect`'s `.cold` catch computes `code = exc->field0 + 0x0B`); `CXATHROW=1`
measured exactly `ServerException code=3` at `TemporaryJob::CreateJob`; `CreateJob@0x20c7d0` is
`if (right != 37 && !JhUserRights::Test(...)) throw`; `JhUserRights::Test@libOptions+0x176a0` =
`HEUTestRights(ticket, …) == 1`; and right **27 = `NC.DataAccessServiceRead`**. The denial itself is in
`libheuseradmin.so`: `getShm()@0x33a0` takes the DYNAMIC path (`/_heusrv_shm`) and matches the right NAME
against a 64-entry table our heuserver never populates → `errno=2; return -1` = DENY. **FIX =
`emulator/heurights.c` (`HEU_GRANT=1`, default OFF)** — supplies the shipped full-local-rights state; only
3 rights are tested constellation-wide and each is logged. hwserver then does exactly what the RE
predicted: *No Heidenhain hardware found, going into simulation mode* → ProgrammingStation → DetectSik →
*HW-Type: NONE - simulated* → GetConfigData → … → DetectMainboard again with *Hardware simulation mode
requested by configuration.* (initMode 2 = SimDrives).
**(3) The remaining SIGSEGV was the emulator's OWN crash handler.** Both faulting EIPs resolve (nm on the
built .so) into **`crash_locate`/`hx`** — the handler ran on the faulting thread's exhausted stack
(`esp` page-aligned, fault addr = `esp-4` = guard page) and re-faulted, destroying the evidence and turning
a survivable condition fatal. **FIX = `sigaltstack`**: `altstack_arm()` lazily mallocs a 64 KB per-thread
alt stack (armed from `task_self()`) and `kern_sigaction` now ORs **`SA_ONSTACK`** for the wrapped fatal
handler. crash count 0 → hwserver completes its run-up.

**Also fixed en route:** hwserver's own config now loads — it needs `-f=` (its documented "configuration
data file" option) with an **absolute** path (it converts `\`→`/` but does NOT resolve the `SYS:` volume),
plus staging of the files `Hardware.cfg` REFERENCES (`SYS:\TABLE\DevTable.hwd` → `/mnt/sys/TABLE`,
uppercase); run_appstart_fex.sh now stages `table/` and creates `/tmp/__use_network_useradmin`, and it now
actually BUILDS `cxathrow.so` (previously preloaded but never compiled, so silently ignored).
New diagnostics: **`HEROSCALL_HSTHEX=1`** (raw wire hex — `msascii()` collapses the deciding bytes) and the
HST **`QI`** line (Q_ident resolution, since some clients treat it as a decision:
`IsExtControlPresent()` is literally `q_ident("AppStartMaster") != -1`).

**★ NEXT FRONTIER (measured on the crossing run):** the NC startup cycle has not yet completed —
`StUpStartupCycleAck`/`0xB700E0` = 0, `FipsUI::EnterPowerInterrupt` = 0, `HideStartupPicture(0x404705C0)` = 0,
`PromActivateNotifyMsg(0x404703E0)` = 0, `SkMgrActivate(0x028a0200)` = 0, `startupPicVisible` = 1 (prom still
correctly waiting). That is exactly the downstream checkpoint list, and it is now reachable for the first
time. Full RE + evidence: **`docs/re/appstart-subsystem-sequencing-gate-re.txt`** (§4k = the crossing);
see [[project-appstart-gate-is-fmprocessstate-initialized]].

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
