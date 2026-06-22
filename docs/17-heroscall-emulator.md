# 17 — The heroscall emulator (Track B: running the i386 control)

This is the live record of **Track B** — running the *unmodified* i386 TNC640
control directly (natively on x86_64, or under user-mode translation on ARM64)
by emulating the HeROS kernel API in userspace instead of loading `heros.ko`.

It supersedes the "TRANSLATION PORT ROADMAP" sketch in `CLAUDE.md` with measured
results: the NCK (`ipo_progstation.elf`) now boots through **five** successive
blockers and runs deep into its own initialisation before hitting the first
*application-level* dependency (the configuration server).

## How it runs

```
ld-linux.so.2 --library-path <sysroot> \
  LD_PRELOAD="herosapi_shim.so heroscall_emu.so" ipo_progstation.elf -p=~/IPO IPO -k=NC -M
```

- `herosapi_shim.so` — fakes `open("/dev/herosapi")` / `/dev/events` (device stub).
- `heroscall_emu.so` — interposes libc `syscall()` and emulates HeROS syscall
  **222** (`heroscall`, `cmd = 0x1234_00NN`). Everything else passes to the real
  kernel via raw `int 0x80`.

The control issues `heroscall` through libc `syscall()`, so an LD_PRELOAD shim
catches every call — **no kernel module and no qemu patch are needed.** This was
verified natively on x86_64 (no emulation in the loop at all).

## The heroscall ABI (recovered from `heros.ko` + live param dumps)

`heros_entry(cmd, param_ptr)` is a pSOS-style RTOS dispatcher. Command map (low
byte of `cmd`): `01 T_ident · 02 T_start · 09 T_name · 0a Q_create · 0d Q_send ·
0e Q_read · 10 Ev_send · 11 Ev_receive · 15 Sm_create · 18 Sm_request ·
21 M_create · 22 M_ident · 23 M_attach · 24 M_detach · 26 Sys_setenv ·
27 Sys_getenv · 29 P_ident · 2a P_childstat`.

Param-struct layouts that matter (confirmed by decompiling the kernel handlers
*and* dumping the live structs with `heroscall_probe2.c`):

| cmd | input | output |
|---|---|---|
| `Sys_getenv` 0x27 | `p[0]`=name, `p[2]`=out buffer, `p[4]`=buf size | value→`p[2]`; return 0 / −errno |
| `M_ident` 0x22 | `p[0]`=name (≤17 bytes, `strncpy 0x11`) | region id in **eax** (−2 if not found) |
| `M_attach` 0x23 | `p[2]`=region id, `p[0]`=req addr | mapped addr in **eax** (`do_shmat`) |
| `T_ident` 0x01 | name inline (`p[0..1]`, zero ⇒ self) | task id in **eax** |
| `Sm_create` 0x15 | name/attrs | sem id in **eax** |

## The blocker chain (each fall reveals the next)

| # | Blocker | Cause | Fix |
|---|---|---|---|
| 1 | `open("/dev/herosapi")` | no heros.ko device | `herosapi_shim.so` returns a stub fd |
| 2 | `PciHardware::Exception` (SIGABRT) | `M_ident("IPO_SHARED_MEMORY")` + `M_attach` returned 0 → null mapping | emulator: `M_ident`→nonzero id, `M_attach`→a real 64 MB zeroed `mmap` |
| 3 | `FProcess` assert `argSkip < argc` (fprocess.cpp:184) | launched with no argv | pass `-p=~/IPO IPO -k=NC -M` |
| 4 | empty `Sys_getenv` | env not served | return the real boot values (below) |
| 5 | **"Invalid Command Option -k"** (misleading) | `IPO::ReadCmdOptions` → `CfgMailslot::GetData(0x2A00C1,"NC")` failed | **OPEN** — needs the config server (see below) |

`-k=NC` is a *valid* option (`CheckCommandLineOptions(this,"SnrdDckhptiuGMvA")`
passes; usage text confirms `-k - Defines IPO-Subsystem`). The error is emitted
*after* a failed config lookup, not by the option parser.

## HeROS environment values (from the control's own boot scripts)

Lifted verbatim from `heros5/bin/../application` + `appproduct`:

```
SYS=/mnt/sys   OEM=/mnt/plc   USR=/mnt/tnc   OEME=/mnt/plce (after mount)
EXECDIRH=/mnt/sys/heros5/bin   EXECBAT=/mnt/sys/batch/heros5
SYS_NAME=SYSTEM:   OEM_NAME=PLC:   OEME_NAME=PLCE:   USR_NAME=TNC:
APPSTART=AppStartMP.elf   STARTFILE=TNC640heros.txt
```

The programming-station IPO process definition (`batch/TNC640heros.txt`):
`FmLoadProcess(processName:="~/IPO", commandLineOptions:="-k=NC -M",
ifDefined:=["programming_station"], imagePath:="…/ipo_progstation.elf")`.

## Blocker #5 — the configuration subsystem (current frontier)

`CfgMailslot::GetData` (libbackend-server.so) decompiles to
`CfgMailslotQueue::CreateQueue(...)` then `CfgMailslotQueue::GetData(...)` — it is
a **client that requests config objects from a config *server* over a HeROS
mailslot queue**. Running IPO standalone, there is no server to answer, so the
"NC" channel-group lookup returns error 42 and IPO aborts.

This is the first blocker that is **not** a kernel/RTOS primitive: it is
inherently multi-process. The config data itself *is* present in the extraction
(`config/ChannelCfg.atr`, `default/oem/config/channel.cfg`, dozens of `.cfg`).

The genuine path forward is therefore the **multi-process route**: run the config
*server* alongside IPO and let them talk. That experiment was run, and it pins the
real ceiling precisely (below).

## Blocker #5 investigation — the 2-process experiment (ConfigServer + IPO)

The config server is **`ConfigServer.elf`** (process `~/cfgserver`, launched
`-f=%SYS%\config\jhconfigfiles.cfg -i=Nc` per `batch/TNC640heros.txt`).
`jhconfigfiles.cfg` lists the machine-config DB (`SYS:\config\jh.cfg`, `…\tnc.cfg`,
`ChannelCfg.atr`, `AxisCfg.atr`, …) — all present in the extraction.

Run standalone under the emulator (with `SYS`/`OEM`/`USR` pointed at the config
tree), `ConfigServer.elf`:
1. resolves env identity, parses `-f`/`-i` ✓;
2. sets up async-signal handling — a burst of `As_mask` (heroscall `0x13`) +
   `P_signal` (the `libheros` `sigchildcatcher`) ✓;
3. then **blocks in an infinite `Ev_receive` for event `0x80000`** — the
   `FProcess` *startup-synchronisation barrier*. A child normally waits here for
   its parent (`AppStartMP`) to grant the next startup state. Standalone, nobody
   sends it, so it never reads config.

`Ev_receive` returns the received event word in **eax** (kernel: `task[0x21]`;
valid conditions 1/2). The emulator's no-op stub returns 0 ⇒ "no event" ⇒ with an
infinite timeout the process busy-loops. An **experimental** coarse grant
(`HEROSCALL_GRANT_EVENTS=1` → return the requested bits) pushes it past the
barrier — but then `ConfigServerProcess::MainContext` → `FThread::EvalContextThread`
asserts **"Context could not be created"** (fthread.cpp:546).
`FThread::CreateContextFromCallback` builds an `FEvent` + `FWaitableEV` and
**asserts that the registered waitable's id equals the event id** — coarse stubs
can't satisfy that.

**Conclusion (the concrete ceiling):** running the *genuine multi-threaded* HeROS
server processes requires faithfully reimplementing the HeROS **task / context /
event / waitable runtime** — tasks as real threads, with `Ev_*`/`Sm_*`/`Q_*` as
real blocking primitives whose ids and wake semantics match what the `FThread`/
`FEvent`/`FProcess` framework asserts on. This is not one bug; it is an entire
subsystem (a HeROS-compatible userspace RTOS), and ConfigServer is one of ~30
constellation processes. That is the practical meaning of the documented
"infeasible to fully port" ceiling. The single-process emulator (this doc's main
result) is the tractable, proven part: it runs a genuine control binary through
its whole kernel-API init.

## The RTOS runtime (`heros_rtos.c`) — faithful blocking primitives

`heroscall_emu.c` stubs each primitive (return success); that makes the genuine
multi-threaded framework busy-loop or assert. `heros_rtos.c` replaces the stubs
with a **real, cross-process RTOS runtime**:

- **State in `/dev/shm/heros_rtos_ctl`** — a `MAP_SHARED` segment that survives
  `fork()`+`execve()`, so the whole constellation (AppStart + children) shares one
  RTOS namespace. Everything inside is referenced by INDEX, never pointer. Lazy,
  idempotent init on the first heroscall (the LD_PRELOAD constructor doesn't fire
  reliably under the explicit-loader invocation).
- **Tasks** — per-thread ids (`T_ident` self maps `(tgid,tid)`→a unique id, cached
  in TLS; named lookup via `T_name`/`T_ident`). This is essential: the old constant
  id aliased every thread.
- **Events** — per-task event word; `Ev_send(task,bits)` ORs bits in + `FUTEX_WAKE`;
  `Ev_receive(want,cond,timeout)` `FUTEX_WAIT`s until `cond` (1=ALL,2=ANY) is met,
  returns the caught word, clears it. Real blocking, no spin.
- **Semaphores** (`Sm_*`), **queues/mailslots** (`Q_*`, ring buffer + futex),
  **named shared regions** (`M_*` → per-name `/dev/shm` files). All futex-blocking.

**Measured result on ConfigServer:** the runtime works. ConfigServer now allocates
**two tasks with distinct ids** (main + a worker thread it spawns), the startup
event wait **blocks** (3 `Ev_receive`, vs 8000 spins under the stub), and it runs
through env + IPC setup (`Q_create`/`Q_ident`/`Q_send`) + the `As_mask` async-signal
burst. It then stops in the worker thread's `sigchildcatcher` / async-signal setup
(`P_signal(ASCHILD)`) with a SIGSEGV/stack-smash — a crash **present with the stub
emulator too**, i.e. the next unimplemented subsystem: the **async-signal layer**
(`As_send`/`As_receive`/`As_mask`/`P_signal`). Debugging it needs a backtrace (the
box has no gdb/strace — next step is a no-sudo gdb or an interposed sigaction +
glibc `backtrace()`).

This is the genuine "make it bootable" substrate, incrementally proven: kernel-API
init (doc above) → blocking RTOS primitives (here) → task creation (next) → the
config server answering IPO's mailslot query.

### Fault-locator + the task-creation handshake (current frontier)

The box has no gdb/strace and glibc `backtrace()`/`dlsym` pull GLIBC_2.34 (control
glibc is 2.31). So `heros_rtos.c` carries an **opt-in fault-locator**
(`HEROSCALL_BTRACE=1`): it interposes `sigaction()`/`signal()`, forwards via raw
`rt_sigaction` with its own restorer trampoline (no dlsym), and on a fatal signal
prints the faulting **EIP + addr + /proc/self/maps** — EIP→lib+offset→function
resolved offline against the (unstripped) `.so`s.

It located ConfigServer's worker-thread SIGSEGV precisely: `libbackend.so`
**`_run(uint,void*)`** dereferencing a **NULL task context** (`*(*ctx+0x84)`,
addr=0x84). `_run` is the HeROS task entry. The creation path is:

```
FThread::EvalContextThread:
  tid = t_create(name, stack, msgsize, flags, prio, _, _run)   // entry = _run
  ctx = malloc(size); ctx[0]=FThread*; ctx[4]=name; ctx[32]=args
  t_start(tid, size, ctx);   free(ctx)        // ctx delivered, then freed
```

Key: **HeROS tasks are libheros *pthreads*** — `t_create`→`t_create_ex` does
`pthread_create`. The created pthread blocks, and **`t_start` delivers the context
buffer to it**, which it then runs as `_run(size, ctx)`. `t_start` ABI (from the
wrapper): `syscall(222, 0x12340002, {p[0]=size, p[2]=ctx, p[4]=tid})`. The stub
`T_start` returns success without copying/delivering `ctx`, so the pthread runs
`_run` with a dead/zero context → the NULL+0x84 fault.

**The `T_create`/`T_start` rendezvous — IMPLEMENTED, working.** Recovered from
libheros `sub_D330`/`t_create_ex`:
- `t_create_ex` does `pthread_create(sub_D330, arg)` (arg[3]=parent task id,
  arg[4]=tid-out), then blocks on `ev_receive(0x80000)`.
- The task pthread (`sub_D330`) issues `T_create` (cmd 0x00) — param `p[2]=msgsize,
  p[6]=&arg_out, p[8]=&taskid_out, p[10]=ctx_buf, p[12]=parent` — which **blocks
  until started**, then calls `_run(arg_out, ctx_buf)`.
- The emulator: `T_create` writes the task id to `*p[8]`, records the delivery slots,
  sends `0x80000` to the parent, and blocks the pthread on a per-task futex.
  `T_start` (cmd 0x02; `p[0]=size, p[2]=ctx, p[4]=tid`) memcpys the context into the
  task's `ctx_buf` (carrying `ctx[0]=FThread*`), sets `*arg_out=size`, wakes it.

Result: ConfigServer now creates+starts **five tasks** (0x101–0x105) cleanly, the
`_run` NULL-context crash is gone, and it runs a real **internal multi-task
constellation** — inter-task `Ev_send`/`Ev_receive`, queues, ~100 `As_mask` async
calls. The next blocker is a different control-side stack-smash in that
async-signal-heavy task activity (config files still not opened). The fault-locator
confirms it's not the emulator (the `T_start` memcpy is bounded/safe); locating it
needs the **async-signal subsystem** (`As_send`/`As_receive`/`As_mask`/`P_signal`,
all still stubbed) implemented, or deeper stack forensics.

**Chain so far:** kernel-API init → blocking RTOS primitives → **task creation
(done)** → async signals (next) → config read → the mailslot serve loop.

## Async signals + the mailslot transport → ConfigServer boots its constellation

The "async-signal-heavy stack-smash" turned out **not** to be the async layer at all.
Implementing `As_*` left the crash unchanged (`HEROSCALL_AS_DELIVER` on/off identical).
The fault-locator's "stack smashing detected" was a *secondary* abort: the real fault is
an **uncaught C++ exception** — `FMailslotQueue::Write` (`fmailslotqueue.cpp:243`) asserts
*"Bad Message Transport!"*, constructs an `FBackEndError`, throws, and `terminate()` runs.
Root cause: the **queue ABI was wrong on every field**.

**Async signals (done, verified).** ABI from `heros.ko` `{As_send 0x12, As_mask 0x13,
As_read 0x14}` + libheros `{as_send, as_mask, as_catch, as_enable}`:
- `As_send`  `p[0]=target` (`-1`=self), `p[1]=bits` — OR into the target's *pending* word;
  if `(mask | 0x2c00000) & newbits`, deliver **SIGUSR1 (10)** to the target's thread via
  `tgkill` (+ sig 18 for the `0x400000` bit) so its ASR runs.
- `As_mask`  `p[0]=&val`, `p[2]=op` (`0`=clear, `1`=add, `2`=set ≡ `as_enable`); the
  resulting/old mask is written **back through `*p[0]`** (the i386 caller reads it there).
- `As_read`  `p[0]=&req` (`0`=all of mask), `p[2]=&out`; `caught=(req|0x2c00000)&pending`,
  `pending&=~caught`. `as_catch(0)` flushes all pending via this.

Each task carries `as_pending`/`as_mask` words in the shared segment. Verified end-to-end:
`As_send task 0x100 bits 0x800000` → `As_read caught 0x800000`. `HEROSCALL_AS_DELIVER=0`
disables real signalling.

**Mailslot queues are string-named, messages are variable-length blobs.** The genuine
ABI (`heros.ko` `Q_create/Q_ident/Q_send/Q_read_ex` + libheros `q_create/q_ident/q_send/
q_read/q_receive`):

| call | params | notes |
|---|---|---|
| `Q_create 0x0a` | `p[0]=name-str, p[2]=depth, p[3]=flags` → id | `strncpy_from_user(name, p[0], 0x11)` — keyed by **name** |
| `Q_ident 0x0b`  | `p[0]=name` ("queue" or "queue.process") → id / `-0x13` | splits at last `.`, matches the queue name |
| `Q_send 0x0d`   | `p[0]=msg ptr, p[2]=size (≤0x8000), p[4]=QID, p[6]=4/5` | message = serialised `GMessage` byte blob |
| `Q_read 0x0e`   | `p[0]=out buf, p[2]=maxsize, p[6]=timeout, p[7]=QID` → size | |

The old emulator treated `p[0]` as the queue id and the message as 8 inline dwords — so
every `Q_send` targeted a bogus queue and `Write` asserted. `heros_rtos.c` now models
queues as `struct qmsg{len; data[16384]}` slots (string-named, drop-oldest on full).

**Auto black-hole queues (`HEROSCALL_AUTO_QUEUE=1`, default on).** `QueueHeLogger` and
`QEvtServer` are owned by *peer processes* absent in a standalone run; `Q_ident` fails, and
the control blindly uses the `-0x13` as a queue id → strict `Write` asserts. `Q_ident` now
auto-creates a sink queue when a name isn't found, so fire-and-forget sends succeed.

**Result — ConfigServer boots its full constellation, no crash.** Worker tasks
`0x101–0x106` (threads under the one pid) create the genuine service queues:
`CfgFileMan` (0x303), `EditThreadQue`/`EditThreadNotify` (0x304/5), `QSikInterface`
(0x306), **`CfgServerQueue` depth 100 (0x307 — the config request queue IPO's
`CfgMailslot` targets)**, `QDongleService` (0x309), `AppStartMaster` (0x30a), with
inter-task `Ev_send`/`Ev_receive` throughout. The queue-ABI fix didn't just stop the
crash — it let the config server stand up the exact request queue that blocker #5's
client connects to.

**Next blocker — the HeLogger registration handshake.** The main thread (task `0x100`)
sends a register `GMessage` to `QueueHeLogger`, then loops `Q_read` on its temp reply
queue (`0x301`) waiting for the logger to ACK — which no peer sends standalone, so it
blocks (or, with the debug cap, spins). `HEROSCALL_QREAD_MAXWAIT=<ms>` caps "forever"
`Q_read` waits to observe past it (default `0` = faithful block). To pass it: synthesise
the logger ACK (decompile the HeLogger client's reply format) or run the real logger peer.
The worker threads build the constellation regardless, so the **2-process config
experiment** — run `ipo_progstation.elf` against this live `CfgServerQueue` — is now the
natural next test. The verbose log is thread-tagged (`[t<tid> hc ..]`).

## Cross-process config IPC works (blocker #5 — the multi-process path)

With ConfigServer standing up `CfgServerQueue` and idling, the **2-process experiment**
(`run_2proc_config.sh`) finally runs: ConfigServer (background) creates the shared
`/dev/shm/heros_rtos_ctl` namespace and its queues, then `ipo_progstation.elf`
(foreground) attaches the *same* namespace. The runtime's futexes are process-SHARED
(no `FUTEX_PRIVATE`), so cross-process wakeups work on the MAP_SHARED segment.

Result — **IPO blows past the standalone blocker #5**:
- `Q_ident "CfgServerQueue" -> 0x306` — IPO resolves ConfigServer's queue *across
  processes* (it was created in the other process);
- `Q_send -> queue 0x306 size 69` — IPO sends its config request to the live server;
- IPO then `Q_read`s its own reply queue, waiting for the answer.

No more "Invalid Command Option -k" / err 42 / immediate exit — the config client now
connects to a real server. `CfgMailslot::GetData("NC")` issues a genuine request.

**Open — ConfigServer's serve loop isn't active yet.** The request sits unread because
no ConfigServer thread is reading `CfgServerQueue`: the five workers are parked on
`Ev_receive 0x01011000` (an event, waiting for "start serving"), and the **main thread
parks in a glibc futex** (`/proc/<tid>/wchan = futex_wait_queue`, but its last *heroscall*
was the temp-queue `Q_delete` — so it's a pthread_cond/join, not the emulator) right
after init, before it signals the workers. ConfigServer also **forks** a supervisor
parent (in `sigsuspend`) over the real multithreaded child. Next: decompile
ConfigServer's main post-init orchestration (what it waits on after the `Q_delete`s —
likely an AppStart "go" / a peer-process registration) so the serve loop activates and a
worker drains `CfgServerQueue` to answer IPO.

## Serve loop activated — ConfigServer reads + processes IPO's request

Backtracing all 7 ConfigServer threads (via `/proc/<pid>/task/*/{syscall,maps,mem}` — yama=1
so the reader forks the server as an ancestor; ConfigServer itself forks a supervisor parent
in `sigsuspend` over the real 7-thread child) showed the serve loop is the HeROS event loop:
`FThread::DispatchEvents → FWaitableList::NotifyEvAll → ev_receive(mask, ANY, -1)`, where the
mask ORs each input-queue waitable's event bit; on an event it runs `FWaitableQueue::Notify →
PollMessage` (non-blocking `Q_read`) → `FModule::DispatchMessage`. So a queue message must
`Ev_send` the queue's event bit to the owner.

The kernel does exactly that: `Q_send`, on enqueue, calls `Ev_sendtcb(owner@+0xb8, bits@+0xe8)`
when the queue is notify-enabled; `Q_create` sets `owner = creating task` and, if `flags & 2`,
`bits = flags & 0xff000000`. `CfgServerQueue` (flags `0x1000003`) → notify `0x01000000` → owner
task `0x100`, matching the receiver's `ev_receive` mask `0x01011000`. The emulator now records
`owner`/`notify_bits` on `Q_create` and `ev_send`s them on `Q_send` (cross-process via the shared
ctl). **Result: the serve loop wakes, `Q_read`s `CfgServerQueue`, reads IPO's 69-byte config
request, processes it, and emits responses to `QEvtServer` (sizes up to 4380).**

**Final gap — reply routing back to IPO.** ⚠️ **[SUPERSEDED — this was a misdiagnosis; see
"Correction (2026-06-22)" below.]** The request carries IPO's reply-queue name as a
length-prefixed string (`"0-0000106CfgM"`), but ConfigServer replies to qid `0xffffffed` (`-0x13`,
a failed resolution) and never `Q_ident`s that name. `FMailslotQueue::Open(astring)` *would*
`q_ident` the name — so the server isn't taking the name from the body; it's using a numeric
reply reference (kernel `Q_send` stores the sender's reply queue in the message node; `Q_read_ex`
returns it) that the emulator doesn't yet surface. Next: decompile the CfgServer message handler
+ kernel `Q_msgpack`/`Q_read_ex` sender metadata, then deliver the sender's reply-queue id on
`Q_read`. Once the reply lands on IPO's reply queue, IPO's blocking `Q_read` wakes via the futex
`__wake_up` path and `CfgMailslot::GetData("NC")` returns — clearing blocker #5. Debug:
`HEROSCALL_DUMPQ=1` hex-dumps queue payloads.

## Correction (2026-06-22): the blocker is service-startup, not reply routing

The "Final gap" section above is a **misdiagnosis**, corrected here after instrumenting the emulator
and decoding the actual runtime (rather than reading more decompiles). **The GMessage transport,
deserialize, type-dispatch, and client-name extraction all work.**

Two diagnostics were added (commit `75c4c92`): `q_send` hex-dumps *failed* sends (`Q_FAIL`, under
`HEROSCALL_DUMPQ`), and `Q_ident` logs empty/NULL-name lookups. Decoding the `0xffffffed` payloads to
ASCII showed they are ConfigServer's own **trace logs**, one reading literally
`#Trace Connect client=0-0000106CfgM, not yet connected, SIK reading in progress` — i.e. the server
*received and parsed* IPO's connect and *knows* its reply-queue name. The empty/NULL-ident diagnostic
**never fired**, so `0xffffffed` is not `q_ident(NULL)`; it is the server's internal
**"not-yet-connected" sentinel** for clients deferred during startup. (`0x1700c0` is the CONNECT message
type, dispatched by `CfgServer::DispatchMessage` → `OnConnectClient`; IPO's first message is a connect,
and it aborts when the connect-ACK never arrives.)

**The real blocker:** ConfigServer cannot finish startup standalone. Its decoded startup-state machine is
`upCreated → upCfgReading → upCfgRead → SIK reading…` then a **dead stop**: the SIK thread `Q_ident`s
`SikServer` (which the emulator auto-creates as a black hole — proving no peer owns it), `Q_send`s a SIK
request there, and **blocks forever on `Q_read(QSikSync)`** for a reply. The SIK/license service
(`TheSikInterface`, `hesikcom_rpc`) is a separate process that isn't running — there is no `SikServer`
binary in `heros5/bin`. (A syntax error in `jhconfigfiles.cfg` drives config load to `HAS_FATAL`, but
that is *tolerated* — startup proceeds.)

`HEROSCALL_SYNC_TIMEOUT=ms` caps forever-`Q_read`s on `*Sync` handshake queues only (e.g. `QSikSync`),
which deadlock precisely because their server peer is absent. With it, startup advances past SIK
(`#Trace SIK: check finished, error=TRUE` → `#Notice Read cycle data now`), then hits the *next*
missing-peer blocker (a loop creating ~1000 `HwsM…` queues that overflows `MAXQ=96`). A full config
round-trip is thus a **chain of missing-service dependencies** — the path forward is to run the service
**constellation** (AppStartMP spawning the SIK/Hws/event-server peers) or add per-service reply stubs,
**not** any change to the message layer.

## Update (2026-06-22): serve loop PROVEN; root cause = host-side peers with no i386 binary

Two real emulator bugs were fixed and the diagnosis was nailed down end-to-end with the 2-process
experiment (`run_2proc_config.sh`, now `timeout -s KILL` + a `head -c` log cap so a spin can't hang
20 min or fill the disk):

1. **`Ev_receive` finite-timeout busy-spin (fixed).** A finite-timeout `Ev_receive` is a *blocking*
   wait (pollers pass timeout 0), but the runtime returned after a *single* disturbed `futex`
   ("best effort on timeout"). The `As_send→SIGUSR1` path interrupts `futex` constantly, so a
   100 s wait (`Ev_receive(0x1000, ALL, 100000ms)`) became a 422 MB busy-spin. Now honors the full
   timeout across spurious wakeups via a `CLOCK_MONOTONIC` deadline.
2. **`MAXQ=96` overflow (fixed → 2048).** Once past SIK, ConfigServer registers a large `HwsMailslot`
   pool; 96 overflowed → `Q_create: table full` retry-spin. Bumped to 2048 (≈393 MB `/dev/shm`
   segment; box has 16 GB shm / 30 GB RAM).

**With both fixed, ConfigServer's serve loop runs and reads IPO's connect.** The dispatch thread
(task 0x100) does `Q_read <- queue CfgServerQueue size 69` — 69 B is exactly IPO's connect message.
So GMessage transport + dispatch are wholly functional (re-confirming the message-layer is NOT the
bug). What's broken is the **reply**: during startup ConfigServer punts *every* client ACK to
`Q_send … qid 0xffffffed`. `0xffffffed == -0x13`, the `Q_ident` "not-found" sentinel — i.e. the
server's "client not-yet-connected, startup incomplete" state. IPO's connect is read but its ACK is
discarded, so IPO blocks forever on `Q_read(0-0000106CfgM / 0x311)`.

**Why startup never completes — the `HwsMailslot` loop is a retry-until-peer, not a probe-scan.**
After the SIK cap, ConfigServer enters an outer loop re-scanning `HwsM<task>N<ctr>` (counter rising
unbounded; ~150 k iterations/run). Decisive observation: the loop's continuation is **independent of
the `Q_ident` result** — it spins identically whether the name auto-creates (non-zero id) or returns
not-found (0x0). So it is waiting on an *external condition* (the Hws server's presence), not on
anything `Q_ident` can return. `HwsM` = `HwsMailslot` (libGMessageHardware); the awaited peer is the
**hardware-message / I/O-simulation server**. (`q_ident` now reports `HwsM*` absent — kept, because
auto-creating ~150 k unique names would re-exhaust even `MAXQ=2048`.)

**Root cause (confirmed).** Both missing peers — `SikServer` (license) and the Hws/IOsim
hardware-message server — **have no i386 binary in `heros5/bin`** (only Hws *client* libs:
`libhwsinterface`, `libhwspathname`, `libHWSPythonInterface`; and **no `AppStartMP.elf`**). They are
the **host-side** pieces (the Windows Qt control suite + JHIO extpack / `jhiosimhostd` — per
`CLAUDE.md`, "the only host piece with no cross-platform binary"). So there is nothing to *run* for
them: finishing ConfigServer startup requires **reimplementing/stubbing the SIK + Hws service replies
inside the emulator** (the "option A" host-reimplementation surface), not launching a constellation.
This is the live frontier. Next tractable sub-step: make one `HwsM` slot resolve to a real queue and
inject a minimal/empty `HwsSrvValue` reply (and a SIK "available/empty" reply) so ConfigServer's
startup-state machine advances to "connected" and binds IPO's real reply queue.

## Runs on ARM64 (2026-06-22): the actual target, box-independent

The whole stack runs on Apple Silicon — lima VM (`tnc`, aarch64) + **qemu-i386** user-mode
translation — reproducing the full 2-process result (the x86_64 box was only ever a faster dev loop).
`emulator/run_2proc_arm64.sh` is the qemu-i386 port of `run_2proc_config.sh`. Two gotchas it bakes in:
(1) **build the `.so` without TLS** — `__thread` pulls a `GLIBC_ABI_GNU_TLS` verneed the control's
glibc 2.31 lacks, so it won't load under qemu (`heros_rtos.c` now uses a non-TLS per-tid cache); build
in-VM with `i686-linux-gnu-gcc -shared -fPIC -O2`. (2) **colon-separated `LD_PRELOAD`**
(`a.so:b.so`) — a space-separated value word-splits through `qemu -E`. Combined sysroot =
`work/target/rootfs` (glibc 2.31) + `heros5` graft. Result: IPO Q_sends its 69-byte connect; the
separate-process ConfigServer wakes and `Q_read`s it — **cross-process shared futexes work under
qemu-i386** — then the same `HwsMailslot` peer-wait. lima ops note: `pkill -x` (not `-f`, which
self-matches "qemu-i386"); detach long runs with `nohup … & ` and poll (a file-redirected long
limactl command idle-drops the SSH).

## Update (2026-06-22 b): the `HwsMailslot` scan was a WRONG not-found code; run-up now completes

The `HwsM<task>N<ctr>` "scan" was **not** a peer-wait — it was `FMailslotQueue::TemporaryQueuename`
(libbackend @0x21dd0) minting a fresh temp reply-mailslot: it formats a candidate name, `q_ident`s it,
and stops when q_ident returns **exactly `0xffffffff` (-1)** = "name free":
`loop: fmt "HwsM<task>N<ctr>"; q_ident; cmp $0xffffffff,%eax; jne loop`. The emulator returned `-0x13`
for not-found, so every candidate looked *taken* → the scan never terminated. **`-1` is the real heros
not-found convention** (most consumers sign-test it, but `TemporaryQueuename` and others exact-compare
`==0xffffffff`). Fix: `q_ident` not-found → `-1` (commit e05feb0). Effect on ARM64: the HwsM scan
vanishes (0 idents), `cfgsrv.log` 80MB→38KB, **the HWS run-up COMPLETES, the worker pool reaches the
idle `Ev_receive(0x1000)` serve state, and ConfigServer stops punting client ACKs to `0xffffffed`
(hundreds → 0).**

**The remaining run-up blocker, fully localized to ONE request/reply.** After the temp mailslot is
created, `HwsMailslotQueue::Create` does `FMailslotQueue::Open(QHWServer)` + `SyncMessage(req,reply)`:
- `Q_ident "QHWServer"` (the hardware-server queue; absent → auto black-hole),
- `Q_send size 83 -> QHWServer` — an HWS **`GetData`** request: GMessage **id 132 (0x84)**, a
  `GMsgString` reply-to = the temp mailslot name **`"HwsM00000100N000"`**, flags `1`/`0x7fffffff`, and
  a 35-byte token string ending in `"GetData"`,
- `Q_read [HwsM00000100N000, timeout 0xffffffff]` — **blocks forever for the reply**.

Capping that read (timeout) does NOT degrade gracefully — `SyncMessage` just **re-reads the reply queue
every interval forever** (it polls; the run-up requires an actual reply, and only proceeds when
`HWSSrvConnected` becomes true). So the next step is a real **QHWServer reply stub** in the emulator:
intercept the `Q_send` to the queue named `QHWServer`, parse the reply-to name + query out of the
request, and post a well-formed HWS reply (an `HwsSrvValue`/`HWSSrvConnected` GMessage carrying a
server handle) to the reply mailslot so `SyncMessage` returns connected. That needs the reply GMessage
layout (RE `HwsMailslotQueue::SyncMessage`/`ReadMessageSync` + `HWSSrvConnected` parsing) — a focused
next chunk. This is the LAST identified run-up blocker before ConfigServer reaches its steady-state
CfgServerQueue dispatch (and can answer IPO's connect).

## Files

`emulator/` — `herosapi_shim.c` (device + open-path logging), `heroscall_probe.c`
(first probe), `heroscall_probe2.c` (full param-struct dump), `heroscall_emu.c`
(stub emulator: `HEROSCALL_VERBOSE`/`HEROSCALL_GRANT_EVENTS`), **`heros_rtos.c`**
(the faithful RTOS runtime), `run_nck.sh` (IPO standalone), `run_cfgserver.sh` (early
stub-emulator config experiment), **`run_rtos_cfgserver.sh`** (ConfigServer under the
RTOS runtime, full constellation), **`run_2proc_config.sh`** (the IPO↔ConfigServer
cross-process experiment). Build each `.c` as an i386 shared object
(`gcc -m32 -shared -fPIC`). Runtime env flags: `HEROSCALL_VERBOSE` (thread-tagged
trace), `HEROSCALL_AS_DELIVER`, `HEROSCALL_AUTO_QUEUE`, `HEROSCALL_SEM_INIT`,
`HEROSCALL_QREAD_MAXWAIT`, `HEROSCALL_SYNC_TIMEOUT` (cap forever-reads on `*Sync` queues),
`HEROSCALL_DUMPQ` (hex-dump queue payloads, incl. failed `Q_FAIL` sends), `HEROSCALL_BTRACE`
(fault locator).
