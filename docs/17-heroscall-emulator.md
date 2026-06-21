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

The genuine path forward is therefore the **multi-process route**: upgrade the
emulator's RTOS primitives (`M_*`, `Sm_*`, `Q_*`/mailslots, `Ev_*`) from
in-process fakes to *real cross-process IPC* (SysV shm/sem/msg keyed by the HeROS
names), then run `AppStartMP.elf` so it spawns the constellation
(IPO + PLC + config server + …), which then share one RTOS namespace and answer
each other's queries. Full boot to the Qt MMI remains the documented
infeasible/legally-barred ceiling, but getting the constellation to start and
exchange config is the next meaningful milestone.

## Files

`emulator/` — `herosapi_shim.c`, `heroscall_probe.c` (first probe),
`heroscall_probe2.c` (full param-struct dump), `heroscall_emu.c` (the emulator),
`run_nck.sh`. Build each `.c` as an i386 shared object (`gcc -m32 -shared -fPIC`).
