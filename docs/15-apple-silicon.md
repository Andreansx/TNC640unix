# 15 — Apple Silicon (ARM64): can we get past emulation?

A deep, honest look at running the TNC 640 programming station on an Apple‑Silicon Mac
(M1/M2/M3/M4, ARM64). It evaluates the tempting idea — *"swap the VM's packages for ARM
builds, and decompile + recompile the proprietary ones"* — against what's actually inside
the VM, then lays out the paths that really work.

> **TL;DR.** The control software is **closed‑source, proprietary, 32‑bit x86 machine code**
> with no source and no ARM build anywhere. On ARM you therefore have exactly two honest
> options: **translate/emulate the x86 code**, or **run it on an x86 machine and treat the Mac
> as a client**. Recompiling the control natively for ARM is **not possible** (no source) and
> reconstructing it by decompilation is neither practically feasible nor legally permitted.
> The good news: what we measured inside the VM makes the *emulation* paths more attractive
> than expected.

---

## 1. What is actually inside the VM (measured, not assumed)

From the live HeROS5 guest (see [04‑guest‑heros5.md](04-guest-heros5.md)):

| Component | Reality | Implication for ARM |
|---|---|---|
| Kernel | `x86_64` **PREEMPT_RT** `5.2.21‑rt15‑yocto‑heros5` | A whole‑machine x86 kernel. Can't run on an ARM kernel; must be emulated or replaced. |
| **Control userland** | **32‑bit `i386`** ELF (`ipo_progstation.elf`, `Xorg`, `busybox` … all *Intel 80386*) | This is **box86 / FEX‑Emu** territory, not just box64. |
| The control itself | ~**348 MB**: `ipo_progstation.elf` (8.6 MB interpolator ≈ NCK), `plc.elf`, `skern.elf` (safety kernel), `dnc.elf`, `hwserver.elf`, `HrMmi.elf` (the MMI), **+ 248 proprietary `.so`** | Proprietary HEIDENHAIN. **No source, no ARM build.** This is *the product*. |
| Proprietary kernel modules | **only `hsci.ko`** (HEIDENHAIN serial controller interface — the cable to real drives/IO) + a userspace FUSE backend `ifsBackend.elf` | `hsci` is **unused on a programming station** (no hardware). So the proprietary *kernel* coupling is essentially nil for our use case — important for option B below. |
| Everything else | stock OSS built for i386: glibc, X11, Qt, glib, libxml2, OpenCASCADE, … | Rebuildable for ARM in principle — but see §3. |

Two facts dominate everything that follows: the control is **(a) proprietary closed binaries**
and **(b) 32‑bit x86**, while its real‑time *kernel* dependence (for the programming station)
is **almost zero** (`hsci` isn't needed without a machine).

## 2. Why "decompile the proprietary parts and recompile for ARM" doesn't work

This is the part of the proposed plan that feels possible but isn't:

- **Scale.** The control is ~87 executables and ~248 shared libraries — the interpolator alone
  is 8.6 MB of optimized C++. Decompilers (Ghidra/IDA) emit *pseudo‑code*, not buildable
  source: they lose types, structs, C++ classes/templates/exceptions, vtable semantics, inlined
  code, and compiler‑specific ABI details. Turning megabytes of optimized C++ back into
  *correct, recompilable, equivalent* source is, at this scale, equivalent to **re‑implementing
  the entire control** — including a real‑time interpolator, a PLC runtime, a safety kernel
  (`skern`), kinematics/collision libraries, and the Klartext MMI. That is a multi‑year,
  many‑engineer effort with no guarantee of behavioural equivalence.
- **Coupling.** These binaries share memory and message buses (`libGMessage*`, `liblsv2`,
  `libplcbin`, `libiocreader…`), assume a specific i386 ABI, and were validated together. A
  partial rebuild doesn't run.
- **Legality.** In the EU you may decompile **for interoperability** (Directive 2009/24/EC
  Art. 6), but the same article **forbids** using what you learn to create a program that is
  *"substantially similar in its expression."* Recompiling the control **is** creating a
  substantially similar program. So this route is off the table legally even where it's
  technically attempted. (See [09‑legal.md](09-legal.md).) Documenting interfaces and writing
  *interop shims* — what this project does — stays on the right side of that line.

**Conclusion:** the proprietary binaries must be *run as x86 code*. The only question is how.

## 3. Why "just get ARM builds of the packages" doesn't get you a TNC

You can absolutely rebuild the **OSS layer** (X, Qt, glibc, OpenCASCADE, …) for ARM — that's
what a Yocto BSP retarget would do. But:

- The OSS packages are the **substrate, not the product**. An ARM HeROS‑like rootfs with no
  `ipo_progstation`/`plc`/`skern`/`HrMmi` is just *a Linux*. The control has **no ARM build**
  and can't be obtained from any archive.
- Worse, the proprietary i386 binaries **dynamically link against i386 builds** of those same
  OSS libraries (`libstdc++.so.6`, `libglib‑2.0`, `libxml2`, …). A process is one ABI: an i386
  `HrMmi.elf` needs **i386** `libstdc++`, not an ARM one. So native‑ARM OSS libraries can't
  satisfy the proprietary binaries in‑process. Native ARM packages only help the parts you're
  *not* trying to run.

So a package swap is useful only as scaffolding for translation (§4‑B), never as a substitute
for the missing ARM control.

## 4. The paths that actually work (ranked)

### A. Full‑system x86 emulation — QEMU / UTM on the Mac  ★ most reliable
Emulate a whole x86 PC in software (QEMU TCG dynamic binary translation; UTM is the friendly
front‑end). The unmodified HeROS image boots; no porting required.

Two real obstacles, both solved:
1. **Install gating.** HeROS only flashes `setup.zip` when it detects **VirtualBox/VMware**
   (`check_vbox()`; PCI `80ee:cafe`). Plain QEMU isn't detected. Fix either by
   **(a)** provisioning once under VirtualBox on *any* x86‑64 Linux box (even a cheap VPS, as in
   [11‑running‑on‑linux.md](11-running-on-linux.md)) and copying the finished disk to the Mac,
   or **(b)** **masquerading as VirtualBox in QEMU** — present a PCI device `80ee:cafe` and
   matching DMI/SMBIOS strings so HeROS self‑installs. (Detection mechanism documented in
   [06‑bridge‑and‑io.md](06-bridge-and-io.md).)
2. **Keypad input.** `putScancodes` is a VirtualBox API. Under QEMU, drive input via QMP
   `send-key` (or the guest `heuinput` FIFO over the guest network). Our keypad's transport
   layer is already pluggable (`vbox` / `heuinput` / `dry`) — adding a `qemu` (QMP) backend is
   a small, self‑contained change. See [12‑keypad‑keymap.md](12-keypad-keymap.md).

**Expectation:** boots and runs; the GUI is **usable but sluggish** (TCG is ~5–20× slower than
native). For the programming‑station use case — editing + test‑run/simulation in **demo mode**
(≤100 NC lines) — that is acceptable. It is *not* suitable for driving real hardware in
real time, but the programming station never does that anyway.

### B. User‑mode x86→ARM translation — FEX‑Emu / box86+box64  ★ closest to the original idea
Run a **native ARM64 Linux VM** (fast, via Apple's Virtualization.framework) and translate only
the **x86 processes** with FEX‑Emu (handles i386 + x86‑64) or box86/box64. The kernel runs
native ARM; only user code is translated.

Why this is genuinely conceivable here (and not for most appliances): the measured proprietary
**kernel** coupling is essentially just `hsci.ko`, which the programming station doesn't use.
The control is "ordinary" i386 userland talking over sockets/shared memory and X — exactly what
user‑mode translators target.

The hard parts (this is a research project, not a weekend):
- The **entire** HeROS userland (X server, Qt, the MMI) is i386, so you either translate all of
  it (≈ as heavy as §A) or stand up a native‑ARM graphics stack and run the HeROS i386 rootfs as
  a translated chroot/container that renders into it. box86 can *thunk* a few hot libraries to
  native (GL/SDL), which helps the GUI; most libs stay i386‑translated.
- Recreate the HeROS runtime environment (init order, the `libGMessage*` buses, the FUSE
  backends, device nodes) outside VirtualBox.
- The interpolator/`skern` expect timing; without the RT kernel they run in the same
  non‑deterministic mode the programming station already uses (no servo loop), so this is
  tolerable.

**Payoff:** potentially *much* faster than §A (native kernel + GUI; only proprietary compute is
translated). **Risk:** high integration effort and fragility.

### C. Rosetta‑for‑Linux — a possible accelerator, not a turnkey path
Apple's Rosetta can translate **x86‑64 user‑mode** binaries inside an ARM Linux VM. It does
**not** run an x86 kernel (rules out booting the HeROS image directly) and is x86‑64‑oriented,
whereas this control is **i386**. So Rosetta isn't a drop‑in, but it could one day accelerate a
§B‑style userland‑translation setup if paired appropriately. Treat it as a future optimization.

### D. Remote x86 + Mac as client  ★ works today, zero new RE
Run the control on **any** x86‑64 Linux box (a spare PC, a home server, or a VPS) using this
repo's `./tnc640` launcher, and use the Mac to **view** (VNC/RDP to the VM) and **drive** it:
our keypad already speaks `heuinput` over a delivery command (e.g. SSH), and HEIDENHAIN's own
LSV2 `KeyPress` works over TCP. This is the **practical answer right now** for an
Apple‑Silicon user who wants to use the control today.

## 5. Recommendation & concrete next steps

1. **Today:** use **D** (remote x86 + Mac client) — already supported by the launcher + keypad.
2. **Highest‑value experiment:** prove **A** end‑to‑end — a UTM/QEMU profile that boots a
   pre‑provisioned HeROS disk on Apple Silicon, plus:
   - a documented **VirtualBox‑masquerade** recipe for QEMU (PCI `80ee:cafe` + DMI), so the
     image can even self‑install under QEMU;
   - a **`--transport qemu`** backend for the keypad (QMP `send-key`) — small and self‑contained.
3. **Research track:** attempt **B** (FEX/box86 chroot of the i386 rootfs on a native ARM Linux
   VM). The minimal proprietary kernel coupling (`hsci` unused) is what makes this worth trying.

## 6. The honest bottom line

There is no way to make the TNC 640 control *native ARM code* — it's closed and x86, and
rebuilding it is both infeasible and not permitted. But "past the emulation requirement" has a
defensible partial answer: **option B** runs the host kernel and as much of the stack as possible
**natively on ARM** and translates **only** the proprietary x86 binaries — which is the spirit of
the original idea, made real. Until that research lands, **option A** (QEMU/UTM) is the reliable
on‑device route and **option D** (remote x86) is the zero‑effort one that works now.
