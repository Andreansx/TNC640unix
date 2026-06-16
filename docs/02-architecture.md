# 02 — System Architecture

## The three layers

The programming station is three layers that the installer stitches together:

1. **Programming‑station software** — the real TNC 640 control software (NCK + PLC + MMI),
   identical to a physical control. Runs **inside** the VM (the HeROS5 Linux guest).
2. **TNCvbBase** — HEIDENHAIN's *host‑side* extension: the **Control Panel/launcher**, the
   virtual **keypad** and **handwheel**, the **PLC‑I/O simulator**, and a custom **VirtualBox
   extension pack (JHIO)**. This is the Windows glue around the VM.
3. **VirtualBox** (Oracle) — the hypervisor. Pinned to **7.1.4** for this package. VMware
   Workstation (via the VIX API) is a documented alternative.

## Block diagram

```
┌─ Windows host ─────────────────────────────────────────────────────────────────────┐
│                                                                                      │
│   Desktop shortcut:  tncvbcntl.exe "<VM>.vbox"                                        │
│                                                                                      │
│   ┌──────────────────────────┐   COM (VirtualBox API)    ┌──────────────────────┐   │
│   │  tncvbcntl.exe           │ ────────────────────────► │  VBoxSVC / VirtualBox │   │
│   │  ("Control Panel",       │   import OVA, set          │      7.1.4            │   │
│   │   Qt6 launcher/wizard)   │   GUI/* extradata,         │        │              │   │
│   │                          │   start VM (fullscreen)    │        ▼              │   │
│   │  spawns ─┐               │                            │  ┌─ HeROS5 guest ──┐  │   │
│   │          ├─ keypad.exe ──┼── keystrokes ────────────► │  │ HEIDENHAIN RTOS │  │   │
│   │          │   (on‑screen  │   (→ guest heuinput FIFO   │  │ (Yocto x86‑64   │  │   │
│   │          │    NC kbd)    │    /tmp/__heuinput)        │  │  Linux 5.18)    │  │   │
│   │          ├─ handwheel.exe┼── TCP :19035 ────────────► │  │                 │  │   │
│   │          └─ jhiosimhostd ┼─┐                          │  │  NCK  (motion)  │  │   │
│   └──────────────────────────┘ │                          │  │  PLC  (machine) │  │   │
│                                 │                          │  │  MMI  (the GUI) │  │   │
│   ┌─ VBoxJHIO extension pack ─┐ │  HGCM host service       │  │  XFCE + SDDM    │  │   │
│   │  iosim.dll / plcmap.dll   │◄┴─ memory‑mapped I/O block │  │  autologin      │  │   │
│   │  (machine I/O simulation) │  ── in "IOsim" shared fldr►│  │       ▲         │  │   │
│   └───────────────────────────┘     synced per PLC cycle   │  └───────┼─────────┘  │   │
│                                                            │          │            │   │
│   Shared folders:  Install │ IOsim │ PLC │ TNC ────────────┼──────────┘            │   │
│   Guest properties: /HEIDENHAIN/{VMUSER/PW, CMD/Cmd, ...} ─┘                        │   │
│                                                                                      │   │
│   The control's SCREEN = the native VirtualBox VM window (fullscreen). VRDE/RDP      │   │
│   on TCP 3389 is also configured for remote viewing (needs Oracle extpack, not       │   │
│   bundled).                                                                           │   │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

## Component responsibilities

| Component | Side | Role |
|---|---|---|
| `tncvbcntl.exe` | host | Launcher + "Control Panel" GUI. Imports/creates/updates the VM through the VirtualBox COM API, starts it as a **fullscreen native VirtualBox window** (configured via `GUI/*` extradata), and launches keypad/handwheel/iosim. Qt6 / QML, includes a "Create Virtual Control" wizard. |
| `keypad.exe` | host | The on‑screen TNC keyboard and soft‑key panel — **the "steering panel"**. Sends keystrokes into the guest's `heuinput` synthetic‑input daemon. |
| `handwheel.exe` | host | Virtual electronic handwheel (emulates HR 520/HR 550). Connects to the guest over **TCP 19035**. |
| `jhiosimhostd.exe` + `iosim.dll` + `plcmap.dll` | host | **PLC‑I/O simulation**: pretends to be the machine's inputs/outputs so the guest PLC reaches operational state. |
| **JHIO extpack** (`VBoxJHIO*.dll`) | host (VBox plugin) | VirtualBox **HGCM host service** that carries the PLC‑I/O block between the guest and `iosim.dll`, synchronised to each PLC scan cycle, via a memory‑mapped file in the `IOsim` shared folder. |
| `tncvbinst.dll` | host (MSI) | Install‑time custom actions: drive the VirtualBox COM API to import the OVA, set shared folders, CPU/RAM/SVGA, create desktop links. |
| HeROS5 guest | VM | The actual control. NCK + PLC + MMI, on a hardened real‑time Linux. Detects VirtualBox and mounts the shared folders / reads guest properties. |

## Key design decisions to keep in mind

- **The guest is the product.** Everything Windows‑specific is *orchestration and I/O
  simulation* around a stock VirtualBox Linux appliance. That is the opening for a port.
- **The base VM ships "empty."** The OVA's `SYS`/`PLC`/`TNC` partitions contain nothing on
  the base image; the NC software (657 MB `target.tar.xz` + config zips) is flashed in on
  first start via the `Install` shared folder. See [03](03-packaging-installer.md).
- **The screen is the native VBox window, not a remote protocol** (locally). VRDE/3389 exists
  for remote viewing only and would need Oracle's extension pack (not bundled here).
- **It is built to run without a dongle or panel** — just degraded to demo limits. So the
  minimum viable system is: the VM + a way to feed keyboard input + (probably) the I/O sim.
