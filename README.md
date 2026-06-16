# TNC640 on UNIX — Reverse‑Engineering & Documentation Project

Goal: understand the **HEIDENHAIN TNC 640 Programming Station** ("PGM‑Platz Virtual" /
virtual control, NC ident **340595**, version **18 SP4**) deeply enough to run it on
**UNIX‑like systems (Linux, macOS)** instead of only Windows.

A HEIDENHAIN TNC 640 is a CNC milling control. The *Programming Station* is the exact
same control software running inside a virtual machine on a PC, so you can write and
dry‑run G‑code / Klartext (conversational) NC programs without a real machine and without
wasting material. HEIDENHAIN ships it as a **Windows‑only** product. This project documents
how it is built and what it would take to run it elsewhere.

> **Status:** Architecture fully mapped **and** the real control now **boots on x86‑64 Linux**.
> Under VirtualBox 7.1 on an x86‑64 host the OVA installs the NC software and reaches the live
> TNC 640 MMI in demo mode, headless — verified, reproducible (see
> [docs/11](docs/11-running-on-linux.md)). Remaining: full input mapping and the PLC‑I/O (JHIO)
> question. See [`docs/`](docs/).

---

## ⚠️ Legal / what is (and isn't) in this repo

This repository contains **only original documentation and analysis**. It deliberately does
**not** contain any HEIDENHAIN or Oracle software. The downloaded package, the VM image, the
extension pack, the manuals, and everything extracted from them are proprietary and are
**git‑ignored**. Full reasoning, including why
the documentation itself is lawful (EU interoperability rights) while redistributing the
binaries is not, is in **[docs/09-legal.md](docs/09-legal.md)**.

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
| [reference/](docs/reference/) | Hard data tables: OVF summary, partition map, file inventories |

## TL;DR of findings

- The product is **VirtualBox + a hardened Linux guest (HeROS5) + a Qt host "Control Panel"**.
  The guest is the real TNC 640 control software; the Windows side is just orchestration.
- The **"steering panel"** (absent when the bare VMDK is booted) is a set of Qt host apps: `tncvbcntl.exe`
  (launcher), **`keypad.exe`** (on‑screen NC keyboard / soft keys), `handwheel.exe`
  (jog wheel), and `jhiosimhostd.exe` (PLC‑I/O simulation).
- Host↔guest is glued by **VirtualBox shared folders** (`Install`, `IOsim`, `PLC`, `TNC`),
  **guest properties** under `/HEIDENHAIN/*`, a guest **synthetic‑input daemon** (`heuinput`),
  a **handwheel TCP port `19035`**, and the **JHIO** PLC‑I/O service.
- It runs in **demo mode with no dongle and no hardware** (limited to 100 NC lines / 10 CAD
  elements) — which means *the VM is meant to run essentially standalone*.
- **Main constraint on ARM64 hosts (e.g. Apple Silicon):** the guest is **x86‑64**; Apple‑Silicon VirtualBox can't run
  x86 guests. The realistic path is a **Linux x86‑64 host**. Two host pieces are Windows‑only
  (the **JHIO extpack** and the **Qt control suite**) and would need to be substituted.
- **Done so far:** on an x86‑64 Linux host the control **installs and boots to the live MMI in
  demo mode, headless**, and responds to injected keypresses (soft keys = F1‑F8). Reproducible
  procedure in [docs/11](docs/11-running-on-linux.md); script `scripts/setup_vm.sh`.
