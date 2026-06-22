# 18 — Handwheel (TCP 19035) & JHIO over Network (TCP 19009): the two remaining host-suite protocols

This document closes the two open reverse-engineering questions for the **Option A** port
(run the stock x86-64 HeROS5 guest in a hypervisor on UNIX/macOS; reimplement the Windows Qt
host control suite natively). With the **keypad** already done (`keypad/`, see
[12-keypad-keymap.md](12-keypad-keymap.md)), the host suite's last two pieces are the
**handwheel** and the **PLC-I/O simulation (JHIO)**. Both turn out to be **plain cross-platform
TCP protocols served by the guest** — neither requires the Windows-only `VBoxJHIO` HGCM
extension pack. This materially reframes the JHIO "deepest blocker" recorded in
[05](05-host-control-suite.md), [06](06-bridge-and-io.md), and [08](08-porting-unix-macos.md).

> Provenance: static analysis of the extracted guest binaries (`work/`) — `strings`, the guest
> config (`/etc/init.d/applaunch`, `etc/sysconfig/portscan-whitelist.cfg`), and Ghidra
> decompiles (`work/re/out/libjhiosimnet.decomp.c`, `work/re/out/ipo_HRSimServer.decomp.c`).
> No HEIDENHAIN code is reproduced; these are interoperability facts (see [09-legal.md](09-legal.md)).

## The decisive map: who serves what

`etc/sysconfig/portscan-whitelist.cfg` (the guest's own firewall whitelist) names the listeners:

| Proto | Bind | Program | Purpose |
|---|---|---|---|
| tcp | `0.0.0.0:19035` | `/mnt/sys/heros5/bin/ipo.elf` | **HEIDENHAIN Handwheel simulation** |
| tcp | `<machine-net eth>:19009` | `plc.elf` (+`libjhiosimnet`) | **JHIO PLC-I/O over network** (not in whitelist; binds the eth/machine-net iface) |
| tcp | `127.0.0.1:19093` | `heuserver` | user server (see CLAUDE.md / Track B) |
| tcp | `0.0.0.0:5900` | `x11vnc` | screen (remote) |

So:

- **Handwheel** = a TCP client (`handwheel.exe`) connecting to the **NCK interpolator**
  `ipo.elf` on **port 19035**. The server lives in `ipo.elf`'s `HRSimServer.cpp`.
- **JHIO PLC-I/O** = the guest **`plc.elf`** (linked with `libjhiosimnet.so`) running a TCP
  **server** on the machine-net interface, **port 19009**; a host I/O-sim connects as **client**.

Both are reimplementable on any OS/arch with a socket. The Windows `VBoxJHIO`/`iosim.dll`/
`jhiosimhostd.exe` stack is **one** transport (HGCM + mmap shared file); the **network**
transport built into the guest is an equivalent, fully cross-platform alternative.

---

# Part 1 — JHIO PLC-I/O over network (`libjhiosimnet`, TCP 19009)

## 1.1 It exists, and the guest enables it itself

`usr/libexec/jhiosimnet` is a start/stop shell wrapper that flips a guest property:

```sh
start)  VBoxControl guestproperty set /HEIDENHAIN/IOSIM/Network on   # (vmware: guestinfo.HEIDENHAIN_IOSIM.Network on)
stop)   VBoxControl guestproperty set /HEIDENHAIN/IOSIM/Network off
```

The real engine is **`usr/lib/libjhiosimnet.so.1.0`** (i386 ELF, 266 functions). It exports the
**exact same block API** as the Windows `iosim.dll` — `_JHIOInternInit`,
`_JHIOInternGet/PutBlock(Ex)`, `_JHIOInternGetHeader`, `_JHIOInternGetBaseOffset`,
`_JHIOInternSetControlReady`, `_JHIOInternSignalPlcCycleDone`,
`_JHIOInternWaitForSimCycleDone`, `_JHIOIsSimulationRunning`, `_JHIOSetPLCRunMode`,
`_JHIOLockMemory`/`_JHIOUnlockMemory` — but transports the I/O block over **TCP** instead of a
memory-mapped file. Locally it still uses POSIX shm (`/jhiosim`, `/jhiosem`) so the in-guest PLC
sees a normal shared-memory I/O block; `libjhiosimnet` mirrors that block to/from the network peer.

Consumers (link/load `libjhiosimnet`): **`heros5/bin/plc.elf`** (the PLC) and
`heros5/bin/hwserver.elf` (hardware server).

## 1.2 Activation & configuration (environment variables)

`/etc/init.d/applaunch` → `set_jhiosim_env()` (runs when `JH_VIRTUALIZATION` is `VBOX`/`VMWARE`):

```sh
eth_iface=$(ip link show | grep "^[0-9]: eth" | ... | tail -1)   # last eth0/eth1
JHIOSIM_GUEST_IF=$eth_iface
JHIOSIM_SVR_PORT=19009
export JHIOSIM_GUEST_IF JHIOSIM_SVR_PORT
```

`libjhiosimnet` reads four environment variables (`getenv`, decompiled in the guest-connection
thread):

| Var | Meaning |
|---|---|
| `JHIOSIM_MODE` | connection mode: **0 = server, 1 = client** (`set_conn_mode`; absent ⇒ derived) |
| `JHIOSIM_GUEST_IF` | server bind **interface** name (e.g. `eth0`) — server mode |
| `JHIOSIM_SVR_IP` | server **IP** to connect to — client mode |
| `JHIOSIM_SVR_PORT` | TCP **port** (default **19009**), `sscanf("%i")`, must be 1..65535 |
| `JHIOLOG_FILE` / `JHIOLOG_LEVEL` | optional debug log file / verbosity (very useful for capture) |

Default in the guest: `GUEST_IF` set, `SVR_PORT=19009`, no `MODE`/`SVR_IP` ⇒ the **guest is the
server**, binding `eth:19009`. A host I/O-sim runs the same role logic in **client** mode:
`JHIOSIM_MODE=1`, `JHIOSIM_SVR_IP=<guest machine-net IP>`, `JHIOSIM_SVR_PORT=19009`.

## 1.3 Wire protocol — a TCP **RPC** (corrected against the live control)

**IMPORTANT correction (live finding, see §3):** the protocol is **not** a passive "header pushed
on connect." The connected server **waits for a request**; it is a bidirectional **RPC**. (My
first model — guest pushes the header on connect — is wrong; a passive `recv` on 19009 of the live
guest returns nothing.) Decompiled from `send_request`/`read_response`/`fcn_id_to_str`:

- **Request (20 bytes, `0x14`)** — `send_request`: `[+0..3] hdr/len`, **`[+4] cFcnId` (1 byte)**,
  `[+8] parm1` u32, `[+0xc] parm2` u32, `[+0x10] parm3` u32. Logged
  `"Send request to %s, cFcnId=%d (%s), parm1=%lu, parm2=%lu, parm3=%lu"`.
- **Response (16 bytes, `0x10`)** — `read_response`: `[+4] cFcnId`, `[+8] rc` u32, `[+0xc] val`
  u32. Logged `"Received %s/IOsim response, cFcnId=%i, rc=%d, val=%i"`.
- **Request magic:** `request[0:4]` must be **`"JHIO"` (0x4f49484a)** or the server logs `"Invalid
  request. Wrong magic!"` and rejects. So a request = `"JHIO"` + `cFcnId`(u32) + parm1 + parm2 + parm3.
- **`cFcnId` opcode map** (recovered verbatim from `fcn_id_to_str`):

  | op | name | r/w | | op | name | r/w |
  |---|---|---|---|---|---|---|
  | 0x0a | `INTERN_INIT` | init | | 0x12 | `SET_CTRL_READY` | write |
  | 0x0b | `SET_PLC_RUN_MODE` | write | | 0x13 | `GET_SIM_ID` | read |
  | **0x0c** | **`GET_HEADER`** | read→740B | | 0x14 | `WAIT_SIM_CYCLE_DONE` | blocks |
  | 0x0d | `GET_BLOCK` | read→block | | 0x15 | `SIG_PLC_CYCLE_DONE` | sync |
  | 0x0e | `PUT_BLOCK` | write | | 0x18 | `GET_DATASIZE` | read |
  | 0x0f | `GET_BASE_OFFSET` | read | | 0x19 | `GET_HEADERSIZE` | read→val=740 |
  | 0x11 | `IS_SIM_RUNNING` | read | | 0x1a | `CLEAR_PUTBLOCKS` | write |

  A host I/O-sim issues `GET_HEADER` (→ the 740-byte map), then per cycle `GET_BLOCK`/`PUT_BLOCK`
  + `SIG_PLC_CYCLE_DONE`/`WAIT_SIM_CYCLE_DONE`, with `SET_CTRL_READY` to assert "control ready".
- **Bulk transfers** ride on specific ops: a **GetHeader**-class reply is followed by the
  **740-byte (`0x2e4`) `JHIO_HEADER`** (`"Failed to send JHIO_HEADER, tx=%d/%d"`, djb2-hashed
  `"JHIO_HEADER hash=0x%x"`); **GetBlock/PutBlock** carry the `lDataSize` I/O data at `lDataOffset`
  as changed-region diffs (`"PutBlocks hash=0x%x (%d bytes)"`, `"No PutBlocks changes found."`,
  `"CLEAR_PUTBLOCKS"`); **SignalPlcCycleDone/WaitForSimCycleDone** are the per-cycle lockstep.

So a host I/O-sim must: connect → issue the RPCs (GetHeader to learn the map, then the per-cycle
GetBlock/PutBlock + cycle handshake) — driven by the client, not received passively.

Header **version** field (`+8`, dword) = 100..400 ⇒ schema versions **1.0 … 4.0** (the lib prints
`// Version 1.0` … `3.0`); a peer announces its version and the other assumes it if older.
Optionally a **machinekeys** file (≤ `0x4000` bytes) is read and shipped alongside.

## 1.4 `JHIO_HEADER` layout (the machine-I/O map)

`print_JHIO_HEADER` treats the struct as a 32-bit `long[]`; index → byte offset = `i*4`. The full
machine-I/O map (this is exactly what a host I/O-sim must understand to place signals):

| idx | off | field |  | idx | off | field |
|---|---|---|---|---|---|---|
| 0 | 0x00 | `lInitialized` | | 0x52/53 | 0x148 | `lStart/LenX48_ADC` |
| 1 | 0x04 | `lPLCRunning` | | 0x54/55 | 0x150 | `lStart/LenX8_9_DAC` |
| 2 | 0x08 | `lVersion` (100..400) | | 0x56/57 | 0x158 | `lStart/LenX148_ADC` |
| 4.. | 0x10 | `PLConfig.PLLogicNr[]` | | 0x58/59 | 0x160 | `lStart/LenPL410_ADC` |
| — | — | `szMapFileName` (char[]) | | 0x5a/5b | 0x168 | `lStart/LenPL510_ADC` |
| 0x48/49 | 0x120 | `lStart/LenInputs` | | 0x5e/5f | 0x178 | `lStart/LenX150` |
| 0x4a/4b | 0x128 | `lStart/LenOutputs` | | 0x60/61 | 0x180 | `lStart/LenX151` |
| 0x4c/4d | 0x130 | `lStart/LenInputBWDs` | | 0x62 | 0x188 | `lSimulationId` |
| 0x4e/4f | 0x138 | `lStart/LenOutputBWDs` | | 0x63 | 0x18c | `lvirtualTNCLicense` |
| 0x50/51 | 0x140 | `lStart/LenX45_ADC` | | 0x64 | 0x190 | `lControlIsReady` |
| 0x65 | 0x194 | `lSynchronousOperation` | | 0x6a | 0x1a8 | `lHSCIConfiguration` |
| 0x66/67 | 0x198 | `lStart/LenX12` | | 0x6b/6c | 0x1ac | `lStart/LenMOP_ADC` |
| 0x68/69 | 0x1a0 | `lStart/LenX13` | | 0x6d/6e | 0x1b4 | `lStart/LenES` |
| 0x6f | 0x1bc | `lDataOffset` | | 0x70 | 0x1c0 | `lDataSize` |
| — | — | `szSPLCMapFileName` | | 0xb1..b4 | 0x2c4 | SPLC In/Out start/len |

(`BWD` = byte/word/doubleword I/O image; `ADC`/`DAC` = analog channels per terminal block
X45/X48/X148/X8_9/X150/X151/PL410/PL510/MOP; `ES` = emergency-stop chain; `SPLC` = safety PLC;
`X12`/`X13` = HSCI expansion. `lControlIsReady` is the "control voltage on / machine ready" flag
the PLC waits for — the field whose absence keeps the machine in "not ready".)

## 1.5 Consequence for the port

The JHIO is **not** Windows-locked. A cross-platform host I/O-sim is:

1. a TCP **client** to `guest:19009` (or run a server and point the guest at it via `JHIOSIM_MODE`);
2. that **send/recv the 740-byte header + the `lDataSize` data block per cycle** and honors the
   cycle handshake;
3. with a machine I/O model writing the input image (the role of `iosim.dll`/`plcmap.dll`; for
   *demo programming + simulation* a minimal "control-ready, no faults" model may suffice — to be
   measured, per [08](08-porting-unix-macos.md) §JHIO).

This is engineering against a now-documented protocol, not a reverse-engineering wall.

---

# Part 2 — Handwheel (TCP 19035, served by `ipo.elf`)

## 2.1 Endpoints

- **Server:** `ipo.elf` (the NCK interpolator) → `HRSimServer.cpp`. Key symbols:
  `InitHrSimulationServer` / `FinishHrSimulationServer`, the listener thread `HrSimThread`,
  `EthernetServer::GetHandwheelInfo(HSCI_MEM_INFO*)`, and per-device I/O
  `HrSim410GetInput`/`HrSim410SetOutput` (HR 410 — simple) and
  `HrSim520GetInput`/`HrSim520SetOutput(…, HRDISPLAYDATA)` (HR 520/550 — with LCD), plus
  `HrSimSignalCycle`. (`PrbHwSimu` = touch-probe simulation, a sibling on the same server.)
- **Client:** `handwheel.exe` — Qt6/QML PE32+. A `QTcpSocket` to **port 19035**; frames are built
  with **`QDataStream` over a `QByteArray`** then `QIODevice::write()`. QML component `HandWheel`
  (`CustomComponentHandWheel`), buttons carry an `operation` string; latching buttons `CTRL`,
  `RAPIDFEED`, `HANDWHEEL`; momentary `F1..F5`; PLC soft keys `PLC_DMG_F01..F20`. The jog wheel
  itself streams increments. Carved QML reference: `work/re/out/handwheel_qml.txt`.

Because the client serializes with `QDataStream` (default **big-endian**, with a stream version),
the **server's read order in `HrSimThread`/`HrSim*GetInput` is the authoritative wire spec** — a
native handwheel must reproduce that field order/types.

## 2.2 Wire protocol (decoded from `HrSimThread` @ `0x54adb0`)

**Server architecture.** `InitHrSimulationServer` spawns the `HrSimThread` HeROS task;
`InitServerSocket()` binds **TCP 19035**. The thread `poll()`s **7 fds**: an internal event fd
(for the per-cycle `HrSimSignalCycle` wakeup, via `ev_send`) + the listen socket
(`HandleNewConnections`) + up to **5 simultaneous handwheel clients**. Per-client state lives in
the `connData[]` array (stride `0x23` dwords = 140 B/`0x8c`); `state` ∈ {0 disconnected,
1 connected, 2 cycle-ready, 3 sent}.

**Input frame (client → server): exactly 33 bytes (`0x21`)** — read in a retry loop until all 33
arrive (`read(fd, buf, 0x21-n)`; a short/!=33 read drops the connection). Layout (all int32 are
host little-endian, the guest being x86):

| off | size | field | meaning |
|---|---|---|---|
| 0 | int32 | `id` | per-connection id; **must equal** the value the server holds for that slot (`DataBuffer+0x3084`) or the frame is rejected — the connect-time handshake key |
| 4 | int32 | `f1` | handwheel state word 1 |
| 8 | int32 | `f2` | handwheel state word 2 |
| 12 | int32 | `f3` | handwheel state word 3 |
| 16 | int32 | `f4` | handwheel state word 4 |
| 20 | int32 | `f5` | handwheel state word 5 |
| 24 | int32 | `f6` | handwheel state word 6 |
| 28 | int32 | `f7` | handwheel state word 7 |
| 32 | byte | `f8` | trailing status byte |

An accepted frame is stored into `DataBuffer[slot]` (+0x30e7…+0x3107) and flagged valid
(+0x307c=1). The cyclic NCK code then exposes it through the per-device getters:

- **HR 410** — `HrSim410GetInput(slot, short& jog, ushort& keys, bool plc)`: a signed **jog
  counter delta** (short) + a **key bitmap** OR-reduced over a 0x17-entry key table
  (`keyMapHR510Nc`/`keyMapHR510Plc`, NC vs PLC key set).
- **HR 520/550** — `HrSim520GetInput(slot, short& jog, ushort& ov1, ushort& ov2, ulong& keys)`:
  jog delta + **two override values** (feed / rapid or spindle) + a wider **key bitmap**
  (`keyMapHR520`). So among `f1..f7` are the jog delta, the two overrides, and the key state.

(The precise `f1..f8`→{jog,axis,ov1,ov2,keys} assignment and the connect handshake that seeds
`id` are the one piece best pinned with a **live packet capture** against a running control — the
structure above is exact; the per-field labels are from the getter semantics.)

**Output frame (server → client):** when a PLC cycle completes (`HrSimSignalCycle` → `ev_send`),
the thread `writeToFd()`s the per-client output block (`connData[slot]+1…`):

- **LED bitmap** — `HrSim410SetOutput(slot, ulong leds)` / `HrSim520SetOutput(slot, ulong leds,
  HRDISPLAYDATA&)` expand a bit-per-LED via `ledMapHR510`/`ledMapHR520` into the block.
- **HR 520/550 display** — `HRDISPLAYDATA`: **80 bytes of screen text** (4 × 20 chars, copied in
  4 chunks of 20), a **cursor** (`row + col*20`, fields +0x90/+0x94), and an **enable** flag
  (+0x98). HR 410 has no display (LEDs only).

The NCK fills the outgoing display/axis state in `PlcIpo::FillHrSimData(HandwheelFromIpoGlobal&)`
(0x30-byte global + per-axis data for 5 axes + three clamped int16 jog values), so the HR
520/550 screen mirrors the live axis positions/feeds.

## 2.3 Building a native handwheel

Same approach as the native keypad: a small cross-platform UI (jog wheel + axis selector + feed
override + the CTRL/RAPID/HANDWHEEL/F-keys) that opens a `QTcpSocket`-equivalent to
`guest:19035` and emits the `HRSimServer` frames. No proprietary assets or binaries required.
See `handwheel/` (to be added alongside `keypad/`).

---

# Part 3 — LIVE validation on a real x86-64 host (`yeen`, 2026-06-22)

The whole Option-A path was run end-to-end on an **Arch Linux x86-64 host (`yeen`/styx, KVM)** with
**VirtualBox 7.2.10**, automated from the proprietary package: import `TNCvbProg.ova` → wire NAT
port-forwards (19035/19009/5900/2222) + shared folders → stage `setup.zip` → headless boot. Results:

- **The real TNC 640 control BOOTS to the live MMI in demo mode.** First boot ran the documented
  installer (Copy setup → Extract archive → RPM packages → Replace → Finalize), deleted `setup.zip`,
  rebooted, and reached the MMI with the **"Shareware"** demo dialog (max 100 NC lines) + the
  "Default OEM passwords detected" notice — exactly as [11-running-on-linux.md](11-running-on-linux.md)
  describes, now reproduced on a fresh Arch box (`scripts/setup_vm_yeen.sh`).
- **Keypad (already shipped) live-validated:** `VBoxManage controlvm … keyboardputscancode 3b bb`
  (F1) + `5b db` (CE) dismissed the Shareware dialog → the control entered **Programming** mode
  ("Power interrupted", control-voltage-OFF soft key). This is exactly the native keypad's
  `putScancodes` transport (`keypad/`).
- **Handwheel server (19035) confirmed live.** `InitServerSocket` binds `0x5b4a0002` =
  **AF_INET : port 0x4a5b (19035)**, `listen(5)`. The server is **silent on connect** (matches the
  decompile — client sends first) and **accepts the 33-byte frame** (`id=0`, the BSS-default
  expected id, is accepted; the connection is held, not dropped). Full **jog-motion** validation
  needs an operating mode (control-voltage-ON), which depends on JHIO control-ready — so the
  handwheel and JHIO are **coupled** (jogging only matters once the machine is "ready").
- **JHIO (19009) confirmed live as an RPC server + the protocol model corrected.** Even booted with
  `/HEIDENHAIN/IOSIM/Network=on`, 19009 is an **active listener that returns nothing to a passive
  `recv` and closes a raw 740-byte push** — because it expects an **RPC request first** (§1.3). This
  live behaviour is what corrected the model from "passive header push" to the request/response RPC.
- **JHIO live, deeper (128-try probe):** sending well-formed RPC requests (correct `"JHIO"` magic +
  the recovered opcodes, read-only `GET_HEADERSIZE`/`GET_HEADER`/…) to the live 19009 **was never
  answered** — the guest's per-connection handler (`accept_client` → handler callback → close) does
  not reply to an unsolicited `GET_*` from a passive client. So the live exchange needs the correct
  **host-side role** (the host as the I/O *peer* the guest's PLC drives/queries, plus the session/
  cycle handshake), i.e. a real **host I/O-sim**, not a passive requester.
- **★ Operational finding:** with `IOSIM/Network=on` **but no host I/O-sim peer connected, the
  control cleanly powers itself off ~3 min after boot** (VBox.log: "Machine state changed to
  PoweredOff", no crash) — the PLC requires its network I/O peer. With **`IOSIM/Network=off`** the
  control is **stable** in demo/Programming mode (no 19009 server, but the programming station works).
  ⇒ the stable "programming station" config is network-off; bringing up the operating modes needs the
  host I/O-sim (which both satisfies the PLC peer and drives the handwheel jog) — the next build.
- Probes used: `handwheel/hr_probe.py`, `jhio/jhio_probe.py` (host-side via the NAT forwards).

**Net:** the platform path is proven (real control runs natively on x86-64 Linux + KVM); keypad is
fully working; the handwheel frame is validated at the server; JHIO's transport is a TCP RPC whose
opcode map + per-cycle client is the remaining work for a host I/O-sim.
