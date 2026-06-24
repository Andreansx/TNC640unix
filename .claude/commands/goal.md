---
description: The current autonomous goal for TNC640unix — crack HrMmi's X/WM expose-render handshake so it draws its first real frame FEX-native, then surface the HrMmi window to the Mac; pushing toward the 92-process constellation + the MMI as a Mac window.
---

# /goal — TNC640unix FEX-native next objective

You are working on TNC640unix. Read `CLAUDE.md` and the recalled memories first; they hold the full
state. This command states the **current objective** and the path to the **ultimate goal**. Work
autonomously: investigate, implement, run, verify, then **commit AND push to origin/main without
asking** (per `feedback-autonomous-commit-push`; no Co-Authored-By trailer, keep Claude-Session).

---

## ULTIMATE GOAL (do not lose sight of it)
Boot the **full 92-process HeROS constellation FEX-native on Apple Silicon (ARM64)** — no
VirtualBox, no x86_64 host — culminating in the real Qt MMI **`HrMmi.elf` rendered as a window on
the Mac**. (The yeen full-system VirtualBox route already reached a live MMI; that is the reference,
NOT the target. The target is the pure FEX-native path — Track B.)

This is a long climb. Each invocation of `/goal` should make **one real, verified, committed step**
up the chain below, then update `CLAUDE.md` + the relevant memory with the precise new frontier.

---

## CURRENT OBJECTIVE (the immediate, well-scoped step — a long climb, expect multiple sessions)
**Crack HrMmi's X/WM expose-render handshake so HrMmi DRAWS its first real frame FEX-native, then
surface that HrMmi window to the Mac.** This is the half of the ultimate goal that produces a visible
MMI: getting a real HrMmi window onto the Mac screen — even standalone (2-proc ConfigServer+HrMmi),
before the full constellation — is the milestone. Fill the constellation in around it afterward.

### Where it stands (verified last session — commit `9cef6fe`, config-reply routing SOLVED)
- `HrMmi.elf` runs FEX-native, passes argv/RTOS/heuserver-auth, connects via `CfgConnectClient`
  (`0x1700c0`, reply-to `".QueueHrMmi"`), `INJECT_ACK` answers `CfgClientIsConnected` (`0x170100`).
- **Config now fully arrives:** `HEROS_CFG_REPLY_ROUTE` (heros_rtos.c q_send, ON in
  `run_2proc_hrmmi.sh`) redirects the empty-named (`""`/`0x30b`) reply to the queue named by the
  reply's leading GMsgString → the **2711B config reply lands on `QueueHrMmi` (`0x30e`)**, HrMmi
  reads it clean (buffer doubling 128→2048→2711, `0x2100018=0`, Unhandled=0, crash=0), M_attaches a
  region, sends follow-up config requests (served directly on `0x316/0x317`), subscribes to
  QEvtServer, and **connects to X** (`connect(AF_UNIX "/tmp/.X11-unix/X99")=0`, Fontconfig active).
- **THE GATE (this objective):** after the X connect HrMmi re-blocks on
  **`Ev_receive(0x03011001, forever)`** = the GUI-render / X-WM expose handshake — the documented
  multi-thread FModule render layer. This is structurally the SAME frontier as AppStartMP's logo
  `0x1000` ping-pong, which was cracked via the `/dev/events` event→fd bridge (commit `bf0b579`).
  ConfigServer stays crash-free; `/etc` guard OK. Clean A/B exists: `CFG_REPLY_ROUTE=0` → 2711B →
  `0x30b`, HrMmi blocked forever, X11=0.

### The task (two phases — do phase A first; A is the hard RE, B is the payoff)
**Phase A — make HrMmi render its first frame.** RE exactly what `Ev_receive(0x03011001)` is waiting
on. Compare to the cracked logo handshake: the logo thread blocked in `select()` on `/dev/events`
because the emulator's `ev_send` woke `Ev_receive` blockers but not `select()` blockers — the fix was
the faithful event→fd bridge (`heros_rtos.c` + `herosapi_shim.c` `evdev_reconcile`: make the
`/dev/events` pipe readable exactly when the kernel would signal). Determine whether HrMmi's render
block is (1) the same missing select()-trigger / event→fd reconciliation on its render thread's
waitables, (2) a genuine X **Expose/MapNotify/ConfigureNotify** the headless Xvfb+openbox never
delivers (HrMmi may wait for the WM to map+expose its top-level window — a real WM mapping/synthetic
expose, or `xdotool`-forced expose, may be needed), or (3) an FModule inter-thread `0x1000`-style
USEREVMASK ping-pong between HrMmi's GUI threads. Use `HEROSCALL_HSTRACE=1` to see which
thread/queue/waitable is involved, IDA (idalib headless: `scratchpad/idalibvenv` +
`work/re/scripts/idadecompile.py`; MCP also available) on `HrMmi.elf` / `libbackend.so` / PLib for
the render dispatcher, and host `strace -f` for the X-socket traffic (writev to X = drawing). Prefer
the faithful RTOS/X fix; if an inject/stub is the only tractable step, gate it behind a default-OFF
`HEROS_*`/`HEROSCALL_*` env knob (like the existing ones) and document it.

**Phase B — surface the HrMmi window to the Mac.** Once HrMmi draws into Xvfb `:99` on the lima VM,
get the window onto the Mac: run `x11vnc` against `:99` in the VM, tunnel to the Mac
(`ssh -fNL 590X:127.0.0.1:5900 …` over `limactl`-exposed SSH, or limactl's port forwarding), and
`open vnc://127.0.0.1:590X` — analogous to the yeen recipe in `project-mmi-live-on-mac-via-yeen`,
but pointed at the FEX-native Xvfb instead of the VirtualBox guest. Even a partial/blank-but-real
HrMmi top-level window visible on the Mac is the deliverable for this objective.

### How to run / reproduce
```
bash emulator/run_2proc_hrmmi.sh        # clean 2-proc: ConfigServer + HrMmi (CFG_REPLY_ROUTE=1 default)
# knobs: CFG_REPLY_ROUTE=0 for the A/B baseline; DUMPQ=1 hex-dumps payloads; HEROSCALL_HSTRACE=1 trace
# the script starts Xvfb :99 + openbox in the VM; screenshot the framebuffer with `import -window root`
```
Build the i386 preloads in the lima VM `tnc` (`i686-linux-gnu-gcc`); the script builds them. The FEX
rootfs is `/var/tmp/lr`. Host `strace -f -e trace=network,writev` is non-invasive (guest open-loggers
perturb timing). Screenshot the Xvfb root window to confirm what HrMmi actually drew.

### Done when (verification criteria)
- HrMmi **advances past** `Ev_receive(0x03011001)` — visible in the trace as it leaving that wait and
  issuing X drawing traffic (`writev` to the X socket).
- An Xvfb screenshot (`import -window root` on `:99`) shows a **real HrMmi top-level window** (MMI
  chrome/widgets, not just the AppStartMP "Startup Status" splash or a blank root).
- That window is **visible on the Mac** via the VNC tunnel (`open vnc://…`).
- ConfigServer stays crash-free; the real `/etc/passwd` md5 guard is unchanged.
- Then: update `CLAUDE.md` (the HrMmi FEX-NATIVE FRONTIER section + blocker chain) and the memory
  `project-hrmmi-executes-under-fex` with the new precise frontier, and **commit + push**.

---

## THE CHAIN ABOVE THIS STEP (the roadmap to the ultimate goal)
Once HrMmi renders + shows on the Mac, the remaining frontiers (in rough order) are:
1. **HrMmi functional render** — beyond the first frame, drive HrMmi to its real MMI screen (Manual
   operation / Programming) rather than a partial/splash window; this likely needs more config
   round-trips and/or the peer subsystems it queries during full UI bring-up.
2. **The constellation around HrMmi** — HrMmi is one of 92 processes. Bring up the peers it talks
   to as needed (IPO/NCK past its HWS/JHIO frontier, evtserver, winmgr, the rest of
   `batch/TNC640heros.txt`). The spawn mechanism is proven: GMessage `FmLoadProcess` injection +
   `HEROS_PCREATE_FEX=1` (native-FEXInterpreter exec) launches subsystems
   (`run_appstart_fex.sh`, `HEROSCALL_INJECT_FMLOAD_SET`). Scale carefully (VM RAM — `MAX` guard).
3. **Full 92-proc boot + drivable MMI** — the ultimate goal: AppStartMP brings up the whole
   constellation FEX-native, HrMmi renders the live operational MMI, surfaced + drivable as a Mac
   window. This is the documented full-system ceiling under userspace; reaching it incrementally per
   `/goal` run is the long climb.

Tackle **one** frontier per `/goal` run. Prefer faithful RTOS/ABI reimplementation over hacks; when
a hack (inject/stub) is the only tractable step, gate it behind a default-OFF env knob and document
it. The deep frontiers (multi-thread FModule GUI handshake) may need substantial RE — that is
expected; make incremental, verified progress and pin the next gate precisely.

---

## GUARDRAILS & KNOWN HAZARDS (read before running)
- **VM degradation tell:** if ConfigServer suddenly aborts at `M_attach "IPO_SHARED_MEMORY"` /
  `PciHardware::Exception` (signal 6) — the lima VM is degraded, NOT your code. Run
  `limactl restart tnc` (cheap; preserves `/var/tmp/lr` + the repo) and retry.
- **FEX detach:** FEXInterpreter survives a dead parent in a detached mount-ns. Never pipe FEX
  through `| head` under `timeout strace` (deadlock). Use `> file` redirects and
  `sudo pkill -KILL -x FEXInterpreter` (×3) to fully clear between runs.
- **FEX /etc leak:** FEX leaks `/etc` *writes* to the real lima guest — heuserver-as-root can wipe
  the lima user from guest `/etc/passwd` and kill SSH. ALWAYS run servers contained in a mount-ns
  with the rootfs `/etc` bound over `/etc`. Recovery recipe: `project-fex-etc-leak-and-vm-recovery`.
- **Never put an apostrophe** in a comment inside the single-quoted `unshare -m bash -c '...'` block
  (it closes the quote and silently breaks the launch). `bash -n` the script after edits.
- `MALLOC_ARENA_MAX=1` for the FEX processes (the `arena_stub` no-op drops arena exclusivity →
  unaligned-atomic SIGBUS races under concurrency).
- The work happens in the lima VM `tnc` (`limactl shell tnc -- <cmd>`); the repo is on the Mac. The
  reproduce scripts handle the VM/host split — read them before improvising.

When you finish a step: state plainly what now works, what the new precise gate is, update
`CLAUDE.md` + the memory, and commit + push. If a frontier turns out to be the documented
infeasible-under-userspace ceiling, say so honestly and pin exactly why, rather than faking progress.
