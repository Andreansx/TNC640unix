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
else winmgr throws uncaught "File not found".** **REAL bar-blocker re-pinned = skmgr's
DRAW GATE:** skmgr connects to X99, serves config (INJECT_ACK on 0x315), attaches the
TR_en region, but BLOCKS on `Ev_receive(0x07011000)` and never loads its `.bmx` /
draws; the softkey strip renders EMPTY. The softkey-area window (0x80001f) isn't
created because Guppy hasn't realized its OEM window
(`jh.gtk.Window(screen='OemScreen')`) to request it — the same self-bind/OEM-realize
gate. Do NOT synthesize `mmi.qHF`. Next: RE what posts Ev 0x07011000 AND why Guppy's
OEM window doesn't realize onto winmgr's now-existing OemScreen. Full chain, every
`INJECT_*`/env knob, and the exact run recipe are in `docs/PROGRESS-LOG.md`.

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
