# 13 — Investigation Log (what was checked → what was learned)

A chronological account of how this system was dissected. Where docs 01–12 give the *findings*,
this is the *journey*: what was inspected at each step, what it revealed, the decisions taken,
and the dead-ends. Tools and exact techniques are in [10-methodology.md](10-methodology.md).

---

## Phase A — Static reconnaissance (on macOS, read-only)

**Checked:** the top-level download `34059518SP4/` — file listing, sizes, types (`file`).
**Learned:** the package is a Windows installer bundle with three parts — `vbox/` (VirtualBox
7.1.4), `base/` (an OVA + an MSI + a custom `.vbox-extpack`), `prog/` (a 1.1 GB `setup.zip` + an
MSI). Two RTF EULAs and four PDFs. Conclusion: this is a hypervisor + a VM + a Windows host layer.

**Checked:** `ReadMe.txt`, `autorun.inf`.
**Learned:** install is meant to go through HEIDENHAIN's *TNCmanager* → "Create Virtual Control";
without a license it runs in **demo mode**. The product is the TNC 640 programming station,
NC ident **340595**, version **18 SP4**.

**Checked:** the OVA (`tar`-listed, OVF extracted) and the JHIO extension pack (it's a gzip tar).
**Learned (key):**
- The VM is **`HeROS5`**, Linux_64, with partitions for SYS/PLC/TNC, two E1000 NICs
  (bridged + host-only), VRDE on 3389, and **USB device filters** for the TE 5xx/6xx/7xx
  keyboards and the **MARX/AKS dongles** — the whole licensing/hardware story in one XML.
- The **JHIO extpack** ships **only Windows DLLs** and declares itself a *"VBoxJHIO host service
  for VirtualBox Heros5 guests."* First sign that one host piece is Windows-locked.

**Checked:** strings of `VBoxJHIO.dll`.
**Learned:** JHIO is a **PLC-I/O simulation** bridge — a memory-mapped block in an `IOsim`
shared folder, synchronized per PLC scan cycle (`SignalPlcCycleDone`/`WaitForSimCycleDone`),
feeding a host `iosim.dll`. So "JHIO" is the *machine I/O*, not the screen or keyboard.

**Checked (dead-end → pivot):** the "control panel" exe `prog/TNC640 (340595).exe`.
**Learned:** it's just an **Advanced Installer bootstrapper**, not the runtime UI. The real
launcher had to be inside an MSI.

**Checked:** the two MSIs via `msiinfo`/`msiextract` (Property, CustomAction, File tables).
**Learned (key):** the host runtime suite lives in the **base MSI** under `…\TNCvbBase\control\`:
- **`tncvbcntl.exe`** = the launcher (`JHCNTLEXE`); starts the VM as a **fullscreen native
  VirtualBox window** via `GUI/*` extradata and spawns the rest.
- **`keypad.exe`** = the on-screen NC/soft-key panel — *the missing "steering panel."*
- **`handwheel.exe`** = jog wheel over **TCP 19035**.
- **`jhiosimhostd.exe` + `iosim.dll` + `plcmap.dll`** = the PLC-I/O sim.
- `tncvbinst.dll` custom actions = an OVA import + shared-folder + guest-property script; the
  base MSI also registers the JHIO extpack and accepts VirtualBox *or* VMware (VIX).

**Checked:** `setup.zip` contents + `SetupHeader.txt` (without full extraction).
**Learned:** it's the actual TNC software image — `target.tar.xz` (657 MB rootfs) + SYS/PLC/TNC
config zips + HeROS RPMs (opencascade, numpy/matplotlib, qupzilla, OPC-UA…). The header
blacklists 32-bit hosts and <1536 MB VMs and runs pre/post shell hooks on `/mnt/sys|tnc|plc`.

**Checked:** the HeROS5 guest filesystem — converted the VMDK to raw, attached read-only with
`hdiutil`, read ext4 with `debugfs` (no mount needed).
**Learned (key):**
- **HeROS 5.18 (Yocto-based RT Linux)**, SysVinit → SDDM autologin → XFCE → the NC MMI.
- The host↔guest bridge: `/etc/init.d/virtualbox` detects VBox (PCI `80ee:cafe`), loads Guest
  Additions, mounts shared folders **`Install`/`IOsim`/`PLC`/`TNC`**, and uses **guest
  properties `/HEIDENHAIN/*`** as a control plane.
- Input is a **synthetic-keyboard daemon `heuinput`** (FIFO `/tmp/__heuinput`, `uinput`).
- **SYS/PLC/TNC are empty on the base image** — the NC software is flashed later. This
  reframed everything: the OVA alone is not a usable control.

**Checked:** the four PDFs (via a sub-agent).
**Learned:** demo-mode limits (100 NC lines / 10 CAD elements), system requirements,
dongle/SIK licensing, documented IPs/ports (network-license UDP 8765, classroom `10.10.10.x`),
TNCremo (`user`/`user` when user-admin off), and that the Control Panel is separable
("start directly in VirtualBox; the Control Panel will then not be included"). One inference
(screen over RDP/3389) was later corrected: the *local* screen is the native VBox window.

**Outcome of Phase A:** the full architecture mapped (docs 01–10). Identified the two hard
blockers for non-Windows: the guest is x86-64, and two host pieces (JHIO extpack, Qt suite) are
Windows-only. Decision: do the dynamic bring-up on an **x86-64 Linux host**.

---

## Phase B — Dynamic bring-up (x86-64 Linux VPS)

**Checked:** the host — `/proc/cpuinfo`, `/dev/kvm`.
**Learned:** Ubuntu 24.04, nested **VT-x** + `/dev/kvm` available → the x86-64 guest can run with
**KVM acceleration**, not slow emulation.

**Checked (experiment):** booted the bare VMDK under **QEMU/KVM**, captured the framebuffer
headlessly via QMP `screendump`.
**Learned:** HeROS boots fine and runs first-boot `runonce` provisioning, then lands at a blank
console — confirming (as predicted) **no NC software on the base image**.

**Checked:** the guest's `/etc/init.d/applaunch` (mounted the raw image with `losetup`).
**Learned (key):** `applaunch` auto-installs `setup.zip` **only when running under
VirtualBox/VMware** (`check_vbox()` sets `JH_VIRTUALIZATION=VBOX`; the install is gated on it)
and the `Install` shared folder is mounted. **Plain QEMU/KVM is never detected**, so it won't
self-install. Decision: use **VirtualBox** (the designed environment) — also required for the
`vboxsf` shared folders and guest properties.

**Checked:** could VirtualBox build on this kernel?
**Learned:** kernel headers present; installed Oracle **VirtualBox 7.1.18** (matches the
product's 7.1.x) and **`vboxdrv` built and loaded** on kernel 6.17.

**Did:** scripted the install — `VBoxManage import` the OVA, NIC→NAT, attach
`Install`/`IOsim`/`PLC`/`TNC` shared folders, set the display guest property; staged `setup.zip`
+ a non-interactive `setup.ini` in the `Install` folder; started **headless** and captured with
`VBoxManage … screenshotpng`.
**Learned:** the chain worked exactly as reverse-engineered — VBox detected → `Install` mounted
→ `applaunch`→`do_setup` ran the **"TNC640 Software Update"** (copy→extract 657 MB→RPMs→replace→
finalize, ~15 min), updated **HeROS .002→.008**, rebooted, and came up in the **live TNC 640 MMI,
demo mode** ("Shareware: max 100 NC lines"). **Milestone: the control runs on x86-64 Linux.**

**Checked:** could the MMI be driven? `VBoxManage … keyboardputscancode`.
**Learned:** yes — injecting PS/2 `0x3B` (→ X keycode 67) acted as **soft-key 1**, dismissing
the demo dialog and reaching the control (Programming mode, "Power interrupted"). Soft keys =
F1–F8.

**Checked (dead-ends):** VirtualBox **guest control** (terminated — HeROS hardening / old GA);
**SSH** as `user` (works, but **password auth is off by default**, matching the manual).
**Did:** powered off, attached the VBox disk with `qemu-nbd`, mounted the HeROS root, and
injected an SSH key for `user` + set SELinux permissive (working-copy only). Booted → **SSH
shell**.
**Learned:** `user` is in all the NC permission groups; `/tmp/__heuinput` isn't present unless
started; **`xdotool` is installed** and can drive the MMI's `:0` display; the `TNC:` drive is
populated with the standard dirs and **HEIDENHAIN's demo NC programs** (`nc_prog/demo/`).

---

## Phase C — Reverse-engineering the keypad (every button)

**Checked:** `keypad.exe` — strings, then carved its embedded Qt resources (the QML is
**zlib-compressed** in the binary; inflated 8 layout/component files).
**Learned:** Qt6/QML layouts (`keypadNc`, `keypadNcVertical`, `keypadMachine`,
`keypadMachineAndNc`, `keypadTE757`) + a C++ `KeyPad` class. Each button carries an
**`operation`** string and an icon; `keypad.buttonClicked(operation)` does the work in C++.

**Checked:** the transport (strings around `__heuinput`, `LSV2`, the C++ class).
**Learned:** two paths — **VM:** start `heuinput`, write `KP/KR <code>` to `/tmp/__heuinput`;
**real WindowsTNC:** **LSV2 `KeyPress`** over TCP.

**Checked:** `heuinput` on the guest (its own `--help`/strings).
**Learned:** token format is `KP <code>` / `KR <code>` where `<code>` is a **Linux input
keycode** emitted via `uinput`.

**Checked (dead-ends → better source):** a static pointer-table scan of `keypad.exe` (only Qt
metadata matched — the map is built in code), and r2 without full analysis (no xrefs). Pivoted:
searched the **guest** for the keymap.
**Learned (key):** the guest ships `/mnt/sys/resource/keymap_te530_*_vbox.xml` — the
**authoritative** map of **`scancode (+modifier) → virtualKey`** for the TE530 keyboard *on
VirtualBox*: 72 special keys.

**Resolved a subtlety:** cross-checking the earlier live test (PS/2 `0x3B`=F1 → soft-key 1, and
the XML lists `HORZSOFTKEY1`=`0x43`) gave the relation **X keycode = Linux keycode + 8**. So the
`heuinput` code = `scancode − 8`, with CTRL/ALT/SHIFT sent as separate keys.

**Validated live:** sent **CE** (X keycode `0x5B`, PS/2 `0x53`) → it **cleared "Power
interrupted"**, advancing the control. The recovered map is correct.

**Outcome:** complete, validated keypad spec in [12-keypad-keymap.md](12-keypad-keymap.md):
every button's `operation`, the transport, and the full `virtualKey → keycode (+modifier)`
table — enough to build a faithful native keypad with no proprietary redistribution.

---

## Running list of decisions & rationale

- **Don't commit any HEIDENHAIN/Oracle artifact.** Repo holds only original analysis; the
  package and everything derived live in git-ignored `work/`. (docs/09)
- **VirtualBox over QEMU** for the real run: the self-install and the `vboxsf`/guest-property
  bridge require a detected VBox/VMware host.
- **Native keypad over running `keypad.exe`**: the original is Windows-only, Wine-fragile, and
  non-redistributable; the input protocol is now fully known. (docs/08, docs/12)
- **Working-copy modifications** (SSH key, SELinux permissive, `user` password) were made only
  to introspect the running guest; they are not part of the OVA and not committed.

## Open questions / not yet done

- **JHIO PLC-I/O** for full machine power-on (control voltage / operating modes) — Windows-only
  today; needs a tolerance test or a Linux reimplementation against the `IOsim` mmap block.
- **NC editor + simulation** end-to-end demonstration (the dry-run use case).
- The **handwheel (TCP 19035)** wire format — not yet captured.
- A **macOS/ARM64** path — only via slow x86 emulation; an x86-64 Linux host is the practical target.
