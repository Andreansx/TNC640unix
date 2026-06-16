# 08 — Porting to UNIX / macOS

This is the project's actual goal. Below is an honest map of what's easy, what's hard, and a
concrete plan. Nothing here has been *attempted* yet — it's the analysis‑backed strategy.

## What's portable vs. Windows‑locked

| Piece | Portable? | Notes |
|---|---|---|
| HeROS5 guest (the control itself) | ✅ yes | Stock VirtualBox Linux appliance; runs on any VirtualBox/QEMU **x86‑64** host |
| VirtualBox | ✅ Linux/mac‑Intel; ⚠️ macOS‑ARM | VirtualBox exists for Linux and Intel macOS. On Apple‑Silicon it only runs **ARM** guests |
| Shared folders / guest properties | ✅ yes | Standard VBox features — reproducible with `VBoxManage` on any host |
| `heuinput` FIFO input | ✅ yes | Pure guest‑side Linux; a Linux host keypad can feed it |
| Handwheel (TCP 19035) | ✅ protocol | Need to reverse the wire format, then any client works |
| **JHIO PLC‑I/O extpack** | ❌ Windows‑only, no source | The hard blocker — see below |
| **Qt control suite** (`tncvbcntl`/`keypad`/`handwheel`) | ❌ Windows binaries | Qt is portable but we only have binaries; options below |
| USB dongle licensing | ➖ optional | Not needed for demo mode; dongles pass through by VID:PID on any host |

## The two real blockers

### Blocker A — the guest is x86‑64; your Mac is ARM64

HeROS5 is **x86‑64 Linux**. To run it you need x86‑64 execution:

- **VirtualBox on Apple Silicon cannot run x86 guests** (the ARM build only virtualizes ARM
  guests). So native VirtualBox on the M2 Max is out for this VM.
- **QEMU/UTM with full x86‑64 emulation (TCG)** *can* boot it on the M2, but it's **slow** —
  and the control software has real‑time expectations (PLC scan cycle, motion), so emulation
  may be sluggish or unstable for the operating modes (programming/simulation likely tolerable).
- **The clean answer is a real x86‑64 host** running VirtualBox/KVM natively with VT‑x. This is
  also what you already proved works (you booted the VMDK on a Linux VM).

→ **Recommendation:** do the bring‑up on an **x86‑64 Linux box**. Use the Mac as the
workstation/SSH client. (You offered access to an x86‑64 machine — that's exactly what's
needed; see "What I need from you" below.)

### Blocker B — the host control suite + JHIO are Windows‑only

On a Linux x86 host we replace the Windows host suite with open equivalents:

- **Launcher** (`tncvbcntl`): trivially replaced by `VBoxManage` (import OVA, set shared
  folders + guest properties, `startvm`) — `tncvbinst.dll`'s actions are effectively a
  `VBoxManage` script (see [05](05-host-control-suite.md)).
- **Screen**: the native VirtualBox GUI window already shows the control; no porting needed.
- **Keypad**: a small Linux on‑screen keyboard that writes the right tokens into the guest's
  `/tmp/__heuinput` (over SSH or VBox guest control). The hardest part is learning the token
  vocabulary the real keypad sends.
- **Handwheel**: optional; reimplement the TCP‑19035 client once the protocol is captured.
- **JHIO PLC‑I/O** (the real unknown): either
  1. **test whether demo programming/simulation works without it** (quite possibly yes for
     pure NC editing + 3D simulation; the "machine operating modes" likely need it), or
  2. **reimplement the host service** for VirtualBox‑on‑Linux. We have a strong spec already:
     the `_JHIOIntern*` block API, the per‑PLC‑cycle handshake, and the memory‑mapped file in
     the `IOsim` shared folder. A Linux daemon could mmap that file and run a minimal "machine
     ready, all I/O nominal" model — `jhiosimhostd.exe` + `iosim.dll`/`plcmap.dll` are the
     reference for the exact semantics.

## Phased plan

**Phase 0 — capture a golden reference (needs a Windows x86 PC, optional but very valuable).**
Install the real product once and record how it wires everything, so we don't reverse‑engineer
blind: `VBoxManage showvminfo` of the created VM, the generated `.vbox`, the exact shared‑folder
names/paths, all `/HEIDENHAIN/*` guest properties, a packet capture of handwheel:19035 and any
keypad traffic, and the contents of the `IOsim` mmap file while running. This single capture
de‑risks Phases 2–3 enormously.

**Phase 1 — boot the guest on x86‑64 Linux (no host suite).** `VBoxManage import TNCvbProg.ova`;
add shared folders `Install`/`IOsim`/`PLC`/`TNC` pointing at host dirs; set the known
`/HEIDENHAIN/*` guest properties; `startvm`. Let it flash `setup.zip` on first boot. Goal: reach
the control MMI on screen. (This is roughly what you did; we'll do it deliberately and
documented.)

**Phase 2 — input.** Get a keystroke into the running control by writing to `/tmp/__heuinput`
(via SSH/guest‑control). Build/borrow a minimal on‑screen keypad. Goal: navigate menus, type an
NC program.

**Phase 3 — decide on JHIO.** Determine empirically what *doesn't* work without the I/O sim. If
programming + simulation are fine, document "demo programming station on Linux works." If the
operating modes are needed, prototype a Linux JHIO host service against the `IOsim` mmap file.

**Phase 4 — package & document** a reproducible Linux setup (scripts + this documentation). A
macOS‑ARM path (UTM/QEMU emulation) can be evaluated separately as a convenience, accepting the
performance hit.

## What I need from you

To make real progress, in rough priority order:

1. **An x86‑64 Linux box** (the most useful thing). Ideally:
   - Ubuntu/Debian 22.04+ (or similar), **bare‑metal or a host with nested‑virt enabled**, with
     **VT‑x/AMD‑V available to the OS** (check `egrep -c '(vmx|svm)' /proc/cpuinfo` > 0).
   - ≥ **8 GB RAM** free, ≥ **40 GB** free disk, `sudo`, internet for `apt`.
   - **SSH access** for me (or you running commands I provide). Tell me the distro/version and
     whether it's bare‑metal or itself a VM (nested virt matters).
2. **The product download** present on that box (the same `34059518SP4/`), since the VM image
   isn't in this repo (and must not be). You already have it locally.
3. **Optional but high‑value: one‑time access to a Windows x86 PC** to do the Phase‑0 golden
   capture (even a throwaway/eval VM). If that's not possible, we proceed by experimentation.
4. **Confirm intent/scope** for licensing: are we targeting **demo mode** (no dongle — simplest,
   fully legal to run) or do you hold a license/dongle you want to use? Demo is the default.

Tell me which of these you can provide and I'll give you exact, copy‑pasteable steps (or drive
it over SSH).

## Reality check

- A **demo‑mode programming station on x86‑64 Linux** is very likely achievable and is the
  right first milestone.
- **Full machine‑operating‑mode behaviour** depends on the JHIO I/O sim, which needs either a
  tolerance test or a reimplementation — solvable but the deepest work.
- **Native, smooth operation on the Apple‑Silicon Mac itself** is the least realistic near‑term
  outcome (x86 emulation cost); plan to use a Linux x86 host and treat the Mac as the client.
