# 01 — Product Overview

## What this software is

A **HEIDENHAIN TNC 640** is a high‑end CNC contouring control for milling machines /
machining centres. HEIDENHAIN sells, alongside the real control, a **Programming Station**
(German: *Programmierplatz*, "PGM‑Platz") — PC software that runs **the exact same control
software** as a real TNC 640, so you can:

- write and edit NC programs (Klartext / conversational and ISO G‑code),
- run the **simulation** (toolpath, 3D material removal),
- learn/train the control's operation identically to the real machine.

Because it is the *same* software, "operation is identical, and the results are compatible"
with a physical control (HEIDENHAIN's wording). The **"Virtual"** variant — the one in this
repo — runs that control software inside a **virtual machine**, so no physical control
hardware (the "MC" main computer) is needed; only a PC.

## The exact product in this repo

| Field | Value | Where it comes from |
|---|---|---|
| Product family | **TNC 640** programming station | manuals, `ReadMe.txt` |
| NC ident number | **340595** | `SetupHeader.txt`, MSI `JhIdent` |
| NC software version | **18 SP4** (version 18, Service Pack 4) | `SetupHeader.txt` `NcVersion`, install path |
| Package name | **34059518SP4** = `340595` + `18` + `SP4` | folder name |
| Guest OS | **HeROS5** = HEIDENHAIN Real‑time OS v5 (5.18.04.002) | `/etc/heros-release`, OVF |
| Base‑VM package version | TNC VBox Base VM **5.18.4.002**, vendor HEIDENHAIN | OVF `ProductSection` |
| Host extension version | TNCvbBase **6.1.9**; JHIO extpack **4.3.0‑r6** | base MSI, `ExtPack.xml` |
| Bundled hypervisor | **VirtualBox 7.1.4‑165100** (Windows) | `Setup/vbox/` |
| Primary manual | "Programming Station for Milling Controls", EN 02/2025, doc id **1369893** | `PGM-Platz_Virtual_en.pdf` |

### Glossary of the numbers/names you'll see

- **PGM‑Platz / Programmierplatz / Programming Station** — the PC product that runs the real
  control software for programming/training.
- **"Virtual" / vTNC** — the variant that runs as a full VM (this product). The same family
  includes iTNC 530, TNC 320, TNC 620, **TNC 640 (340595)**, and TNC7 (817625).
- **vTNC7** — the *successor* generation, NC‑SW **817625** (e.g. version 20 SP1). It moved to
  online/SIK2 licensing. It is **not** what's in this repo, but its German manual was useful
  for cross‑checking; relevant deltas are noted where they matter.
- **HeROS** — HEIDENHAIN Real‑time Operating System; a Linux distribution HEIDENHAIN builds
  (Yocto/OpenEmbedded based for HeROS5) and ships on both real controls and the VM.
- **SIK** — *System Identification Key*: HEIDENHAIN's mechanism for enabling software/feature
  options ("option bits") on a control. On the programming station this is provided by a USB
  dongle (MARX) rather than a hardware SIK card.
- **NCK / PLC / MMI** — the three parts of the control software: **NCK** (NC kernel = motion /
  interpolation), **PLC** (programmable logic controller = machine logic, I/O, M‑functions),
  **MMI** (man‑machine interface = the on‑screen control GUI). All three run inside the guest.
- **TE 5xx / 6xx / 7xx** — HEIDENHAIN operating‑panel keyboard units (the physical control
  keyboards). Their USB vendor ID is `0x1091` (HEIDENHAIN).

## How HEIDENHAIN intends it to be installed (Windows)

Per `ReadMe.txt` / `LiesMich.txt`, the modern path uses HEIDENHAIN's **TNCmanager** tool:
*"Select **Create Virtual Control** → select this installation file → confirm license →
execute. Without software protection the programming system will start in **demo mode**."*

The package also contains a classic standalone installer wizard (`Install TNC640
(340595).exe`) that installs VirtualBox → the TNCvbBase host extension → the programming‑station
software, in sequence. Either way the end result is a VirtualBox VM plus the host control suite.
Details: [03 — Packaging & installer](03-packaging-installer.md).

## Why a port is interesting (and hard)

The control software is just **Linux in a VirtualBox VM**. That is portable in principle.
But HEIDENHAIN's *host side* (the launcher, the on‑screen keypad/handwheel, and a custom
VirtualBox extension pack that bridges PLC I/O) ships as **Windows‑only binaries**, and the
guest is **x86‑64** (a problem on Apple‑Silicon Macs). The rest of these docs map exactly
what each piece does and what substituting it would require:
[08 — Porting to UNIX/macOS](08-porting-unix-macos.md).
