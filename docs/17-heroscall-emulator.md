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
`HEROSCALL_QREAD_MAXWAIT`, `HEROSCALL_BTRACE` (fault locator).
