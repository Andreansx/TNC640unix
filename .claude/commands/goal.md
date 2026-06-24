---
description: The current autonomous goal for TNC640unix — get HrMmi to CREATE + render its first window FEX-native by satisfying the operational-peer constellation handshake it blocks on, then surface that window to the Mac as a STANDALONE NATIVE XQuartz window (no VNC); pushing toward the 92-process constellation + the live MMI as a Mac window.
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
**Get `HrMmi.elf` to CREATE + render its first real window FEX-native by satisfying the
operational-peer constellation handshake it currently blocks on, then surface that window to the Mac
as a STANDALONE NATIVE XQuartz window (no VNC).** Producing a real HrMmi top-level window on the Mac
screen — even standalone (2-proc ConfigServer+HrMmi, peers stubbed/injected), before the full
constellation — is the milestone. Fill the constellation in around it afterward.

### Where it stands (verified — HEAD `d440cc1`, QEvtServer connect-ACK + render gate RE-PINNED)
- `HrMmi.elf` runs FEX-native, passes argv/RTOS/heuserver-auth, connects via `CfgConnectClient`
  (`0x1700c0`, reply-to `".QueueHrMmi"`), `INJECT_ACK` answers `CfgClientIsConnected` (`0x170100`).
- **Config fully arrives:** `HEROS_CFG_REPLY_ROUTE` (heros_rtos.c q_send, ON in `run_2proc_hrmmi.sh`)
  redirects the empty-named (`""`/`0x30b`) reply to the queue named by the reply's leading
  GMsgString → the **2711B config reply lands on `QueueHrMmi` (`0x30e`)**, HrMmi reads it clean
  (buffer doubling 128→2048→2711, `0x2100018=0`, crash=0), M_attaches a region, sends follow-up
  config requests (served on `0x316/0x317`), and **connects to X** (`connect(AF_UNIX
  "/tmp/.X11-unix/X99")=0`, Fontconfig active).
- **QEvtServer connect-ACK SOLVED** (`HEROSCALL_INJECT_EVT_ACK`, ON in the script): `EvtConnectClient`
  (`0x320081`) → synth `EvtClientIsConnected` (`0x3200A0`, 3 GMsgInt all-0) → `OnEvtConnected@0x324e0`
  success → HrMmi sends `EvtErrorRequest` + a follow-on config request. Crash-free, `/etc` guard OK.
- **THE GATE (this objective) — CORRECTED by RE this session, NOT an X expose:** HrMmi connects to X
  but **blocks BEFORE creating any window** (Xvfb screenshot = 1 flat colour, blank; no
  `XCreateWindow`/`XMapWindow` in the X traffic). It blocks at **`Ev_receive(0x03011001, forever)`**
  waiting on its **operational peers** — it subscribes to **AppStartMaster (`0x308`), IPO/NCK
  (`0x310`), Q_PLC_FRONTSTAGE (`0x30f`), CM/ChannelManager (`0x311`)** and waits for THEIR replies.
  None of those processes run in the 2-proc setup, so the replies never come. The peer subscribes are
  **parallel to** (not gated by) the Cfg/Evt connect-ACKs — so satisfying the connect-ACKs is
  necessary-but-not-sufficient; HrMmi never reaches window creation. ⇒ the FEX-native HrMmi first
  frame is gated on the **constellation peers (IPO/PLC/CM/AppStartMaster)**, not the X/WM layer.
  **XQuartz alone will NOT crack this gate** (no window is created to expose). Clean A/B exists:
  `CFG_REPLY_ROUTE=0` → 2711B → `0x30b`, blocked, X11=0.

### The task (two phases — A is the hard RE that makes a window exist; B surfaces it to the Mac)
**Phase A — make HrMmi CREATE its first window by satisfying the operational-peer handshake.** RE
exactly which reply(s) on `0x308/0x310/0x30f/0x311` HrMmi's `Ev_receive(0x03011001)` is waiting on,
and which gate `XCreateWindow`. This is the constellation-peer frontier (roadmap step 2 brought
forward, because it gates the *first frame*). Two viable approaches, pick per what the RE shows:
  1. **Synthetic peer replies (INJECT-style, the proven pattern).** Like `INJECT_ACK` (Cfg) and
     `INJECT_EVT_ACK` (Evt): RE the message schema each peer would send back to HrMmi's subscribe
     (the "you are subscribed / here is initial state" reply for AppStartMaster/IPO/PLC/CM), build it
     from the `.rodata` schema templates, and post it to `QueueHrMmi`. Gate behind a default-OFF
     `HEROSCALL_INJECT_PEER_ACK`-style env knob (ON in the run script), documented.
  2. **Bring up the minimal real peers.** Launch the actual peer processes (IPO/NCK, a PLC stub, CM,
     AppStartMaster) under FEX via the proven GMessage `FmLoadProcess` injection +
     `HEROS_PCREATE_FEX=1` so they answer HrMmi's subscribes for real. Heavier (VM RAM — use the
     `MAX` guard), but more faithful and reusable for the constellation step.
Use `HEROSCALL_HSTRACE=1` to see the exact thread/queue/waitable, IDA (idalib headless:
`scratchpad/idalibvenv` + `work/re/scripts/idadecompile.py`; MCP also available) on `HrMmi.elf` /
`libbackend.so` / `HrModule::DispatchMessage@0x3d060` for the subscribe→reply→render path, and host
`strace -f -e trace=network,writev` for X traffic (`writev` to the X socket = drawing). Prefer the
faithful fix; if an inject/stub is the only tractable step, gate it default-OFF and document it.

**Phase B — surface the HrMmi window to the Mac as a STANDALONE NATIVE WINDOW (NO VNC — user
directive).** The target is a real macOS window, NOT a VNC desktop-in-a-box. Use **XQuartz in
rootless mode**: install XQuartz on the Mac (`brew install --cask xquartz`; **not currently
installed** — needs a one-time install + maybe a logout), each X11 top-level becomes its own native
Quartz window. Point HrMmi's `DISPLAY` at the Mac's X server over the lima→Mac path: enable XQuartz
TCP (`defaults write org.xquartz.X11 nolisten_tcp 0`, restart, `xhost +`) and tunnel `localhost:6000`
from the VM to the Mac (reverse SSH tunnel via the lima SSH, or `socat`), OR X11-forward. Then HrMmi
draws straight onto the Mac desktop as a rootless window — no Xvfb framebuffer, no VNC. SYNERGY: a
real X server + WM (XQuartz) sends genuine `Expose`/`MapNotify`/`ConfigureNotify` — **once Phase A
makes a window exist**, XQuartz is what shows it natively; and if any *secondary* render step later
turns out to wait on a real expose, XQuartz delivers it where headless Xvfb+openbox does not. So set
up XQuartz EARLY (it's needed regardless), but expect Phase A (the peer handshake) to be what
actually unblocks window creation. Qt/X clients render fonts client-side (XRender glyphs), so
Mac-side fonts are not a blocker; MIT-SHM won't apply cross-host (X falls back automatically — fine
for a UI). Keep the Xvfb path ONLY as a debugging framebuffer for screenshots, never the deliverable.
Even a partial-but-real HrMmi top-level window as a native Mac window is the deliverable.

### How to run / reproduce
```
bash emulator/run_2proc_hrmmi.sh        # clean 2-proc: ConfigServer + HrMmi (CFG_REPLY_ROUTE=1 default)
# knobs: CFG_REPLY_ROUTE=0 = A/B baseline; EVT_ACK=0 = QEvtServer-ACK A/B; DUMPQ=1 hex-dumps payloads
# the script starts Xvfb :99 + openbox in the VM + xwd-screenshots the root at +150s (/tmp/c2_screen.xwd)
# add HEROSCALL_HSTRACE=1 to the MMI env for the compact event/queue/thread trace
```
Build the i386 preloads in the lima VM `tnc` (`i686-linux-gnu-gcc`); the script builds them. The FEX
rootfs is `/var/tmp/lr`. Host `strace -f -e trace=network,writev` is non-invasive (guest open-loggers
perturb timing). Screenshot the X root to confirm what HrMmi actually drew (blank = no window yet).

### Done when (verification criteria)
- HrMmi **advances past** `Ev_receive(0x03011001)` — visible in the trace as it leaving that wait and
  issuing **`XCreateWindow` + `XMapWindow`** then drawing traffic (`writev` to the X socket).
- A screenshot of the X server (Xvfb root via `xwd`, or the XQuartz window) shows a **real HrMmi
  top-level window** (MMI chrome/widgets, not just the AppStartMP "Startup Status" splash or a blank
  root).
- That window appears **on the Mac as a native XQuartz (rootless) window** — NOT a VNC desktop.
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
