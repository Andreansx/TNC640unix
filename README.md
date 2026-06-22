# TNC640 on UNIX — Reverse‑Engineering & Documentation Project

Goal: understand the **HEIDENHAIN TNC 640 Programming Station** deeply enough to run it on
UNIX‑like systems. It already runs on x86‑64 Linux (see status below); the open frontier is
Apple Silicon / ARM64 and full machine power‑on.


> I dont own a windows pc and i cant set up a windows 11 arm vm on my mac (cause the vm isnt arm64), so i wanted to be able to run this natively on Apple Silicon.
> its gonna work on apple silicon ig

A HEIDENHAIN TNC 640 is a CNC milling control. The *Programming Station* is the exact
same control software running inside a virtual machine on a PC, so you can write and
dry‑run G‑code / Klartext (conversational) NC programs without a real machine and without
wasting material. HEIDENHAIN ships it as a Windows‑only product. This project documents
how it is built, what already runs on Linux, and what it would take to go further.

The exact product analysed here is the "PGM‑Platz Virtual" virtual control, NC ident
340595, software version 18 SP4.

> **Status:** Architecture mapped end to end, the real control **runs on x86‑64 Linux**, the
> input protocol is reverse‑engineered, and there's a native keypad plus a one‑command launcher.
> Remaining: the PLC‑I/O (JHIO) question for full machine power‑on. See [`docs/`](docs/).

---

## Quickstart

You supply the official HEIDENHAIN package (see [docs/14](docs/14-install-and-run.md) §2);
this repo is the glue. On an x86‑64 Linux PC with VirtualBox 7.1+ and Python 3:

```sh
git clone https://github.com/Andreansx/TNC640unix && cd TNC640unix
./tnc640 doctor                                    # check prerequisites
./tnc640 keypad --install-deps                     # install the keypad's PySide6
./tnc640 setup --package /path/to/HEIDENHAIN/download   # first time only (~15-20 min)
./tnc640 run                                       # start the control + keypad
```

Everyday use is just `./tnc640 run` (and `./tnc640 stop`). Full manual:
[docs/14 — Install & Run](docs/14-install-and-run.md). It runs in demo mode — no
dongle, no license needed (100 NC lines / 10 CAD elements).

---

## How it boots on Apple Silicon (the ARM64 runtime model)

> This is the design the ARM64 work is building toward. Parts of it are **proven running today** on
> an M‑series Mac (the NCK interpolator, the config server, and the HeROS user/login service all
> execute under it); the full process constellation + GUI is the remaining work. It is
> fundamentally different from — and far lighter than — the original VirtualBox model.

**There are two ways to run this control:**

1. **Full‑system route (the heavy fallback).** Boot the genuine HeROS5 disk image under
   `qemu-system-x86_64` / UTM — a *complete emulated x86 machine* running HEIDENHAIN's own kernel.
   It works, but it simulates an entire computer continuously, so it is slow and power‑hungry. This
   is the only route that needs the original `.vmdk`.

2. **Native‑ish route ("Track B" — what this project is building).** Run the control's *original
   i386 binaries directly* on the Apple‑Silicon CPU, with **no emulated machine and no booted
   control OS**. This is the light path, and the surprising part is everything it does *not* need.

### What Track B does NOT require

- **No recompiling the control.** The original i386 binaries run *unchanged*; they are translated
  i386 → ARM64 **at runtime** by [FEX‑Emu](https://github.com/FEX-Emu/FEX), a userspace JIT (like
  Rosetta, but for 32‑bit Intel). The byte‑identical ARM64 recompiles in [`recomp/`](recomp/) are a
  separate **verification** result — proof we understand the code — **not** how the control runs.
- **No compiling a Linux kernel for ARM64.** We use the stock Ubuntu‑ARM64 kernel that already
  ships in a lightweight Linux VM (`lima`, backed by Apple's hardware `vz` hypervisor — real
  virtualization, not emulation).
- **No HeROS kernel, no `heros.ko`, no boot.** HeROS is HEIDENHAIN's realtime OS; its custom kernel
  API (the `heroscall` gateway, `syscall(222, …)`) is **faked in userspace** by our
  [`emulator/`](emulator/) — a small `LD_PRELOAD` shim that answers the OS calls the control makes
  and passes ordinary Linux calls straight through to the real kernel.
- **No VirtualBox and no `.vmdk`.** Nothing is booted as a guest OS; the control's processes run as
  ordinary (translated) Linux processes.

### The runtime stack

```
macOS (Apple Silicon)
└─ lima VM  — thin Linux ARM64, via Apple's vz hypervisor (hardware‑accelerated, NOT emulation)
   ├─ FEX‑Emu                  — translates i386 → ARM64 instructions at runtime
   ├─ heroscall emulator       — LD_PRELOAD shim: fakes the HeROS kernel API + a shared IPC namespace
   ├─ the original i386 binaries — NCK, config server, the HeROS services, the Qt MMI (HrMmi.elf) …
   ├─ X11 + a window manager   — the display substrate (Xvfb/X + openbox)
   └─ the native keypad / steering panel
```

So there is still *one* VM in the picture — but it is **not** the HeROS guest. It is just a thin,
hardware‑accelerated Linux host, because FEX and the i386 binaries need a Linux kernel underneath.
It runs at native speed and, unlike a full‑system emulator, genuinely **idles** when the control is
waiting for input — which is why it is far easier on battery than the UTM route.

### How you launch it / how you see it

A single launcher script takes the place of the original `tncvbcntl.exe`: it brings up the display,
starts the shared IPC namespace, launches the control processes in dependency order, and opens the
native keypad. The Qt MMI is surfaced to the Mac desktop **as an ordinary macOS window** (via X11
forwarding to XQuartz, or VNC) — no full‑screen VirtualBox console.

The keypad gets *simpler* than on Windows: it speaks the same input protocol (the `heuinput` FIFO
and TCP port `19035`), but now as **local IPC** in one Linux namespace — the entire VirtualBox
host↔guest bridge of the original product disappears.

### Status of this path

Proven on an M‑series Mac today: FEX + the heroscall emulator carry individual control processes
(the NCK interpolator, the config server) through their RTOS/kernel init, and the HeROS user/login
service (`heuserver`) completes its full credential setup. The remaining work is bringing up the
rest of the ~90‑process constellation in dependency order and, at the very top, the Qt MMI. Current
frontier and details: [`docs/16`](docs/16-arm64-decompilation-and-translation.md),
[`docs/17`](docs/17-heroscall-emulator.md), and the project tracker (`CLAUDE.md`).

---

## Legal / what is (and isn't) in this repo

This repository contains only original documentation and analysis. It deliberately does
**not** contain any HEIDENHAIN or Oracle software. The downloaded package, the VM image, the
extension pack, the manuals, and everything extracted from them are proprietary and are
git‑ignored. Full reasoning, including why
the documentation itself is lawful (EU interoperability rights) while redistributing the
binaries is not, is in [docs/09-legal.md](docs/09-legal.md).

## Documentation index

| Doc | Contents |
|---|---|
| [01 — Product overview](docs/01-product-overview.md) | What PGM‑Platz / vTNC is, what the version numbers mean |
| [02 — System architecture](docs/02-architecture.md) | The three‑layer stack and how everything fits; the big diagram |
| [03 — Packaging & installer](docs/03-packaging-installer.md) | Folder map, MSIs, `setup.zip`, install flow, on‑disk layout |
| [04 — Guest: HeROS5 internals](docs/04-guest-heros5.md) | Partitions, boot, init scripts, users, kernel modules, configs |
| [05 — Host control suite](docs/05-host-control-suite.md) | `tncvbcntl` / `keypad` / `handwheel` / `jhiosimhostd` + JHIO extpack |
| [06 — Host↔guest bridge & I/O](docs/06-bridge-and-io.md) | Shared folders, guest properties, input, handwheel, JHIO PLC‑I/O |
| [07 — Networking, licensing, demo](docs/07-networking-licensing.md) | IPs/ports, dongles, SIK, demo limits, system requirements |
| [08 — Porting to UNIX/macOS](docs/08-porting-unix-macos.md) | Blockers, Apple‑Silicon problem, the x86‑host plan, open questions |
| [09 — Legal & redistribution](docs/09-legal.md) | Copyright, EULA/ToS, can the repo be public |
| [10 — Methodology](docs/10-methodology.md) | Exactly how this was analysed (reproducible) |
| [11 — Running on x86‑64 Linux](docs/11-running-on-linux.md) | **Verified** procedure: boots the real control headless under VirtualBox |
| [12 — Keypad: full button map](docs/12-keypad-keymap.md) | Every keypad button + the exact code it sends (the input protocol) |
| [13 — Investigation log](docs/13-investigation-log.md) | The full dissection story: what was checked → what was learned, step by step |
| [14 — Install & run (the manual)](docs/14-install-and-run.md) | **Start here to run it.** Prerequisites, getting the package, `./tnc640 setup`/`run`, troubleshooting |
| [15 — Apple Silicon (ARM64)](docs/15-apple-silicon.md) | Can we get past x86 emulation on M‑series Macs? What's really inside, why recompiling is out, the paths that work |
| [reference/](docs/reference/) | Hard data tables: OVF summary, partition map, file inventories |

Plus working code:
- [**`tnc640`**](tnc640) — the one‑command launcher (`doctor` / `setup` / `run` / `keypad` / `stop` / `status` / `shot`).
- [**`keypad/`**](keypad/) — a native, cross‑platform on‑screen keypad (PySide6) that replaces the
  Windows‑only `keypad.exe`, reproducing both NC panel layouts and sending the same key codes via
  VirtualBox's `putScancodes`. Validated live against the control.

## TL;DR of findings

- The product is VirtualBox + a locked‑down Linux guest (HeROS5) + a Qt host "Control Panel".
  The guest is the real TNC 640 control software; the Windows side is just orchestration.
- The "steering panel" (absent when the bare VMDK is booted) is a set of Qt host apps: `tncvbcntl.exe`
  (launcher), `keypad.exe` (on‑screen NC keyboard / soft keys), `handwheel.exe`
  (jog wheel), and `jhiosimhostd.exe` (PLC‑I/O simulation).
- Host↔guest is connected by VirtualBox shared folders (`Install`, `IOsim`, `PLC`, `TNC`),
  guest properties under `/HEIDENHAIN/*`, a guest synthetic‑input daemon (`heuinput`),
  a handwheel TCP port `19035`, and the JHIO PLC‑I/O service.
- It runs in demo mode with no dongle and no hardware (limited to 100 NC lines / 10 CAD
  elements), so it needs nothing external to start.
- **Main constraint on ARM64 hosts (e.g. Apple Silicon):** the guest is x86‑64, and
  Apple‑Silicon VirtualBox can't run x86 guests. The realistic path is a Linux x86‑64 host.
  Two host pieces are Windows‑only (the JHIO extpack and the Qt control suite) and would need
  to be substituted.
- **Done so far:** on an x86‑64 Linux host the control installs and boots to the live MMI in
  demo mode, headless, and responds to injected keypresses (soft keys = F1‑F8). Reproducible
  procedure in [docs/11](docs/11-running-on-linux.md); script `scripts/setup_vm.sh`.
