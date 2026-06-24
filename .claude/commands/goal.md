---
description: The current autonomous goal for TNC640unix — route ConfigServer's config-DATA reply to HrMmi, then push the FEX-native boot chain toward the 92-process constellation + the MMI as a Mac window.
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

## CURRENT OBJECTIVE (the immediate, well-scoped step)
**Make ConfigServer's config-DATA reply land on HrMmi's real input queue so HrMmi receives the
config and advances past the config-load block.**

### Where it stands (verified last session — commits up to `dbaa209`)
- `HrMmi.elf` runs FEX-native, passes argv/RTOS/heuserver-auth, connects via `CfgConnectClient`
  (type `0x1700c0`, reply-to GMsgString `".QueueHrMmi"`), `INJECT_ACK` answers
  `CfgClientIsConnected` (`0x170100` → `OnCfgConnected`), HrMmi reads the 34B ACK and sends its
  config requests (159/225B) to `CfgServerQueue` (`0x303`).
- HrMmi **parses config cleanly** now (the `0x2100018` GMessage abort was a Q_read too-big ABI bug,
  fixed: too-big ⇒ return `-12` + `errno=ENOMEM`, no dequeue — `emulator/heros_rtos.c` q_read).
- **THE GATE:** ConfigServer READS HrMmi's requests (with the doubling), computes a ~2711B config
  reply, but `Q_ident`s the reply-to to **`""` → queue `0x30b`** (a run-up black hole), NOT
  `QueueHrMmi` (`0x30e`). HrMmi gets 0 replies → blocks at `Ev_receive(0x03011001)` forever.
- The **QEvtServer relay is the WRONG model and is DEFAULTED OFF** — its broadcasts are
  ConfigServer's `EvtSendEvent` TRACE stream (`0x320221`) + a 4380B `0x40320461` + HrMmi's own
  subscribe echo, none of which `HrModule::DispatchMessage@0x3d060` handles (forwarding them →
  fatal "Message was not handled"). Do **not** revive the relay as the config path.

### The task
Find why ConfigServer resolves the per-client config-DATA reply-to to `""` and make the reply land
on the client's real input queue (`QueueHrMmi` / `0x30e`). Two viable angles — pick based on what
the RE shows:
1. **Faithful (preferred):** RE ConfigServer's per-client registration / reply-queue mapping (how
   it records a connected client's reply-to and uses it for `CfgGetData` replies, vs. the `INJECT_ACK`
   connect path that already extracts `.XxxQue` reply-tos). Make the per-client state map the client
   → its `QueueHrMmi` so the DATA reply routes correctly. Use IDA (idalib headless:
   `scratchpad/idalibvenv` + `work/re/scripts/idadecompile.py`; MCP tools also available) on
   `libConfigSystem.so` / `libbackend-server.so` — look at `CfgServer::OnGetData` /
   `CfgServer::OnWriteData@0x225510` / the reply-queue resolution, and the `OnConnectClient` vs
   `Initialize` client-registration split documented for blocker #5.
2. **Inject-style (pragmatic, like `INJECT_ACK`):** intercept ConfigServer's config-data reply in
   the emulator and re-route it from the `""`/`0x30b` black hole to the requesting client's
   `QueueHrMmi`. The emulator already extracts `.XxxQue` reply-tos for CONNECTs — extend that to
   DATA replies. Gate it behind an env knob (default OFF) like the other `HEROSCALL_*`/`HEROS_*`
   knobs so it is reproducible and non-destabilizing.

### How to run / reproduce
```
bash emulator/run_2proc_hrmmi.sh        # clean 2-proc: ConfigServer + HrMmi (RELAY off by default)
# knobs: DUMPQ=1 hex-dumps message payloads; RELAY=QueueHrMmi restores the old (crashing) relay for A/B
```
Build the i386 preloads in the lima VM `tnc` (`i686-linux-gnu-gcc`); the script builds them. The FEX
rootfs is `/var/tmp/lr`. Trace with `HEROSCALL_HSTRACE=1` (compact event/queue/thread trace) and host
`strace -f -e openat,newfstatat` (non-invasive; guest open-loggers perturb timing).

### Done when (verification criteria)
- HrMmi reads the config-DATA reply on `QueueHrMmi` (`0x30e`) — visible in the trace as a `Q_read
  0x30e` of the ~2711B (or the real per-request) config payload, `0x2100018=0`, `Unhandled=0`,
  `signal=0`, no abort.
- HrMmi **advances past** `Ev_receive(0x03011001)` — i.e. it proceeds to the next stage (GUI/Xlib
  init, X connect, or the next config round-trip) rather than blocking forever.
- ConfigServer stays crash-free (no `free(): invalid pointer`, no `CfgUnitOfMeasure` throw).
- Then: update `CLAUDE.md` (the HrMmi section + blocker chain) and the memory
  `project-hrmmi-executes-under-fex` with the new precise frontier, and **commit + push**.

---

## THE CHAIN ABOVE THIS STEP (the roadmap to the ultimate goal)
Once the config-DATA reply reaches HrMmi, the next frontiers (in rough order) are:
1. **HrMmi GUI/render layer** — Xlib init (fonts/theme `tnc640_theme.xrs.zip` already load for
   AppStartMP's logo), connect to X (Xvfb `:99` + openbox in the VM), the X/WM expose-render
   handshake. This is the documented multi-thread FModule GUI-sync layer (the `0x1000` USEREVMASK
   ping-pong; the logo handshake was cracked via the `/dev/events` event→fd bridge, commit
   `bf0b579` — apply the same faithful-RTOS approach to HrMmi's render handshake).
2. **Surface the render to the Mac** — once HrMmi draws into Xvfb on the lima VM, get the window
   onto the Mac (e.g. VNC/x11vnc on `:99` tunneled to the Mac, analogous to the yeen recipe in
   `project-mmi-live-on-mac-via-yeen`).
3. **The constellation around HrMmi** — HrMmi is one of 92 processes. Bring up the peers it talks
   to as needed (IPO/NCK past its HWS/JHIO frontier, evtserver, the rest of `batch/TNC640heros.txt`).
   The spawn mechanism is proven: GMessage `FmLoadProcess` injection +
   `HEROS_PCREATE_FEX=1` (native-FEXInterpreter exec) launches subsystems
   (`run_appstart_fex.sh`, `HEROSCALL_INJECT_FMLOAD_SET`). Scale carefully (VM RAM — `MAX` guard).

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
