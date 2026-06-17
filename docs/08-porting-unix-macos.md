# 08 — Porting to UNIX / macOS

This is the project's goal: run the programming station on UNIX-like systems instead of only
Windows. The picture below separates what is done, what is portable, and what still needs work.

## Current status

- **x86-64 Linux: working.** Under VirtualBox 7.1 on an x86-64 Linux host, the OVA installs the
  NC software and boots into the live TNC 640 MMI in demo mode, fully headless, and the MMI
  responds to injected keyboard input. Verified and reproducible — see
  [11-running-on-linux.md](11-running-on-linux.md).
- **Remaining:** a complete input solution / shippable on-screen keypad; the PLC-I/O (JHIO)
  question for full machine power-on; and ARM64 hosts (including Apple Silicon).

## What's portable vs. Windows-locked

| Piece | Portable? | Notes |
|---|---|---|
| HeROS5 guest (the control itself) | ✅ yes | Stock VirtualBox Linux appliance; runs on any VirtualBox/QEMU **x86-64** host |
| VirtualBox | ✅ Linux / Intel macOS; ⚠️ ARM | Exists for Linux and Intel macOS. On ARM64 it only virtualizes **ARM** guests |
| Shared folders / guest properties | ✅ yes | Standard VBox features — reproducible with `VBoxManage` on any host |
| Keyboard input to the MMI | ✅ yes | The MMI reads X keyboard events; delivering them works on Linux (see below) |
| Handwheel (TCP 19035) | ✅ protocol | Reimplementable once the wire format is captured |
| **JHIO PLC-I/O extpack** | ❌ Windows-only, no source | The hard blocker for full machine behaviour — see below |
| **Qt control suite** (`tncvbcntl`/`keypad`/`handwheel`) | ❌ Windows binaries | Qt is portable, but only binaries exist; substitute natively (below) |
| USB dongle licensing | ➖ optional | Not needed for demo mode; dongles pass through by VID:PID on any host |

## The ARM64 / Apple-Silicon constraint

The guest is **x86-64 Linux**. The reverse-engineering for this project began on an Apple-Silicon
(ARM64) Mac, which surfaced the core constraint for any ARM64 host:

- **VirtualBox on ARM64 cannot run x86 guests** — the ARM build only virtualizes ARM guests, so
  native VirtualBox is not an option for this VM on Apple Silicon.
- **QEMU/UTM with full x86-64 emulation (TCG)** *can* boot it on ARM64, but it is **slow**, and
  the control software has real-time expectations (PLC scan cycle, motion), so the machine
  operating modes may be sluggish or unstable. Pure NC editing + simulation is more tolerant.
- **The robust answer is a real x86-64 host** running VirtualBox/KVM natively with VT-x.

**Recommendation:** target an **x86-64 Linux host** for real use; treat ARM64 (Apple Silicon)
as an emulation-only convenience path, or use a remote/headless x86-64 host with a thin client.

> **Deep dive:** [15-apple-silicon.md](15-apple-silicon.md) examines the ARM64 question in full —
> what's actually inside the VM (the control is proprietary **32-bit x86**, with near-zero
> proprietary *kernel* coupling), why recompiling it for ARM is infeasible **and** not legally
> permitted, and the paths that do work (QEMU/UTM with a VirtualBox-masquerade; FEX/box86
> user-mode translation; remote x86 + Mac client).

## Input on Linux: keystroke injection vs. the original keypad

**How input actually reaches the control.** The MMI is a Qt/X application that reads its keyboard
from the guest's X server. On a real programming station the Windows `keypad.exe` is just one
source of those key events. On Linux the *same* events can be delivered two ways, both verified:

- **Hypervisor-level scancode injection** — `VBoxManage controlvm <vm> keyboardputscancode …`
  (works headless; confirmed that **F1–F8 = the 8 horizontal soft keys**).
- **In-guest X injection** — `xdotool` against the MMI's `:0` display (the guest ships
  `xdotool`, and local X access is permissive), allowing named keys, typed strings, and mouse
  clicks on soft keys by coordinate.

This is **genuine keyboard input** to the MMI through the normal X input path — not a hack or a
"fake." It is exactly the channel any keyboard (including a reimplemented keypad) uses. So a
Linux launcher does **not** need `keypad.exe` to be functional: it needs (a) an on-screen panel
UI and (b) a way to deliver the correctly-mapped key events — and (b) is already solved.

**Could the original `keypad.exe` run on Linux?** It is a Windows Qt6 binary. In principle it
could run under **Wine**, but it manages/contacts the VM through the **Windows VirtualBox COM
API** plus HEIDENHAIN-specific channels, which do not translate cleanly under Wine against a
Linux VirtualBox — fragile at best, and doubly so on ARM64 (x86 Windows under Wine + CPU
emulation). It also **cannot be redistributed** (proprietary). So shipping or running the
original keypad is not a sound basis for a cross-platform launcher.

**Does decompiling `keypad.exe` make sense?** Limited value:

- It is compiled C++ (Qt6). Decompilation yields unmaintainable pseudo-code, not real source.
- Its embedded **QML/resources** (layouts, soft-key labels, icons) can be *extracted* for visual
  reference, and static analysis can reveal the **exact key codes and the protocol** it uses to
  deliver keys to the guest. *Studying* it for that is worthwhile to make a native keypad
  faithful. *Running or "porting" the binary itself* is not.

**Recommended approach — a native, open keypad.** Build a small native on-screen panel (any
toolkit) that reproduces the TNC key layout and injects the mapped key events into the guest via
the proven channel. This is cross-platform (Linux, macOS, any arch), needs no proprietary
redistribution, and is robust. The open work item is completing the **PC-key → TNC-key map**
(partly documented; F1–F8 confirmed) — best informed by the captured keypad protocol plus the
HEIDENHAIN keyboard documentation.

## The JHIO PLC-I/O question

For full **machine operating modes** (power-on / control voltage / axis motion), the guest PLC
expects an I/O peer every scan cycle. On Windows that is the JHIO extension pack feeding
`iosim.dll`; there is no non-Windows build and no source (see
[05](05-host-control-suite.md) and [06](06-bridge-and-io.md)). Two paths:

1. **Determine empirically what needs it.** Pure NC-program editing and toolpath simulation in
   Programming/Test mode may not require it; the operating modes likely do. Measure this before
   investing in a reimplementation.
2. **Reimplement the host service** for VirtualBox-on-Linux. The interface is well understood:
   the `_JHIOIntern*` block API, the per-PLC-cycle handshake
   (`SignalPlcCycleDone`/`WaitForSimCycleDone`), and a memory-mapped file in the `IOsim` shared
   folder; `jhiosimhostd.exe` + `iosim.dll`/`plcmap.dll` are the behavioural reference.

## A native launcher (replacing the Windows host suite)

On an x86-64 Linux host the Windows host suite maps to open equivalents:

- **Launcher** (`tncvbcntl`): replaced by `VBoxManage` (import OVA, set shared folders + guest
  properties, `startvm`) — the install-time custom actions are effectively a `VBoxManage` script.
  See `scripts/setup_vm.sh` and [11](11-running-on-linux.md).
- **Screen**: the native VirtualBox window shows the control; for headless/remote use, capture
  with `VBoxManage … screenshotpng`, or enable VRDE (needs Oracle's extpack) / a VNC into the
  guest.
- **Keypad / handwheel**: native reimplementations as described above.

## Reproducing / extending this

- An **x86-64 Linux host** with hardware virtualization (`vmx`/`svm`) available to the OS —
  bare-metal or a VM with **nested virtualization** enabled; ≥ 8 GB RAM, ≥ 40 GB free disk, KVM.
- The HEIDENHAIN package, obtained legitimately — it is **not** in this repo
  (see [09-legal.md](09-legal.md)).
- VirtualBox 7.1.x. Full steps in [11-running-on-linux.md](11-running-on-linux.md).
- **Optional, high value:** a one-time capture on a Windows install ("golden reference") of how
  the real `keypad.exe`/`handwheel.exe` talk to the VM — the cleanest way to nail the keypad and
  handwheel (TCP 19035) protocols for native reimplementations.

## Reality check

- A **demo-mode programming station on x86-64 Linux** is achieved and is the right first
  milestone; NC editing + simulation are the natural next demonstrations.
- **Full machine-operating-mode behaviour** depends on the JHIO I/O sim — solvable, but the
  deepest work.
- **Native, smooth operation on ARM64 (Apple Silicon)** is the least realistic near-term outcome
  because of x86 emulation cost; an x86-64 Linux host (local or remote) is the practical target.
