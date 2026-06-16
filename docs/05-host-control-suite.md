# 05 — Host Control Suite (the Windows side)

Installed to `…\HEIDENHAIN\TNCvbBase\control\`. All are **Qt 6 / QML** apps shipped as
**Windows PE binaries** (with 32‑bit copies under `control\win32\`). These are exactly the
pieces missing when you boot the bare VMDK — and the pieces that must be substituted for a
non‑Windows host.

> All sizes/strings below come from static analysis of the extracted binaries (no execution).

## `tncvbcntl.exe` — the launcher / "Control Panel" (2.26 MB)

Internally the installer calls this **`JHCNTLEXE` / "PANELEXE"**. It is what the desktop
shortcut runs: `tncvbcntl.exe "<VM>.vbox"` (the install/update path passes `--reboot`).

What it does (from its symbols/strings):

- **Drives VirtualBox through the COM API** (`CLSIDFromProgID`, `VBOX_E_*` error strings,
  `:/sys/virtualbox`) — it can import/create/update the VM and **start it** (`startVm()`).
- **Controls the VM window via `GUI/*` extradata** (these are VirtualBox's own per‑VM GUI
  settings, written into the `.vbox`): `GUI/Fullscreen`, `GUI/AutoresizeGuest`,
  `GUI/LastGuestSizeHint(WasFullscreen)`, `GUI/Scale`, `GUI/RestrictedRuntimeMenus`,
  `GUI/SuppressMessages`, `GUI/Input/MachineShortcuts`. → **the control's screen is the
  native VirtualBox VM window, shown fullscreen with menus locked down.**
- Sets **display guest properties** `/HEIDENHAIN/CFG/Display/{Mode, Accel3d, LastResolution,
  LastVMSVGA}` — the Control Panel's "Display" tab (Normal / Scale‑no‑3D / Full + a 3D‑accel
  toggle) maps onto these.
- **Launches the companion apps** — it references `keypad.exe`, `handwheel.exe`,
  `jhiosimhostd.exe` by name and has signals like `setKpNmSeparate(bool)`,
  `sigselectGUI(QString)`, `pressSelectGUIButtonInMainWindow`.
- Built with `QWizard`/`QWizardPage` — i.e. it includes the **"Create Virtual Control"**
  wizard flow.

Documented Control‑Panel tabs (from the manual): **License conditions**, **Hardlock**
(USB/network dongle, license server IP), **Keypad** (launch at startup, layout e.g. "Vertical
NC keypad"), **Handwheel** (launch / always‑on‑top), **Display** (the 4 modes above),
**NC Share** (map `TNC:` to a Windows drive), **Network** (Auto‑Hostname, shows IP). Buttons:
**Stop** (shut down VM, keep panel), **Shutdown** (both), **Break** (hard power‑off).

## `keypad.exe` — on‑screen TNC keyboard / soft‑key panel (1.99 MB) — *the "steering panel"*

The virtual operating panel: numeric keys, axis keys, mode keys, and soft keys, clickable
with the mouse. From its strings:

- It targets the guest's **`heuinput`** synthetic‑input service: references
  `/etc/init.d/heuinput start`, the FIFO **`/tmp/__heuinput`**, and writes commands to it
  (`… >>/tmp/__heuinput`). So a click on a virtual key becomes a synthetic keystroke inside
  the guest.
- Talks to the VM host stack (`VBOX_E_NOT_SUPPORTED`, `Vix` for VMware) and references
  **LSV2** (HEIDENHAIN's line‑protocol for file/DNC over TCP) and `127.0.0.1` / ICMP — used
  for probing/connecting to the control.
- Fully localized (German/Polish/Finnish/Czech error strings observed).
- `chip_transport*` strings relate to the CrypToken dongle handling.

The exact transport that delivers keystrokes from the host keypad to the guest FIFO is the
key open question for a port (candidates: VBox guest control, an SSH/LSV2 channel on the
host‑only net, or a small guest forwarder). See [08](08-porting-unix-macos.md).

## `handwheel.exe` — virtual electronic handwheel (657 KB)

Emulates a HEIDENHAIN portable handwheel (HR 520 / HR 550 per the vTNC manual). It opens a
**`QTcpSocket` to TCP port `19035`** ("…on port 19035") — i.e. it connects to a handwheel
server inside the guest (over the host‑only network) and streams jog increments / axis
selection / feed‑override. **Port 19035 is a concrete, documented‑by‑binary integration
point.**

## PLC‑I/O simulation: `jhiosimhostd.exe` + `iosim.dll` + `plcmap.dll`

This is the "machine" that the guest PLC talks to. On a real control the PLC reads/writes
physical I/O over HSCI; here those I/O are **simulated on the host**.

- **`iosim.dll`** (28 KB; installed name of `JHIOsim64.dll`) — the simulation implementation.
  Two API layers:
  - low‑level block API: `_JHIOInternInit`, `_JHIOInternGet/PutBlock(Ex)`,
    `_JHIOInternGetHeader`, `_JHIOInternSetControlReady`, `_JHIOInternSignalPlcCycleDone`,
    `_JHIOInternWaitForSimCycleDone`, `_JHIOIsSimulationRunning`, `_JHIOSetPLCRunMode`,
    `LockMemory`/`UnlockMemory`, `GetHeaderSize`/`GetDataSize`.
  - named‑signal API: `JHIOGetAddressByName`, `JHIOGetAddressByClamp` (Klemme = terminal),
    `JHIOGet/Set{Byte,Short,Long,Float,Logic}Value(Wait)` — read/write individual PLC I/O by
    name or terminal.
- **`plcmap.dll`** (39 KB) — maps PLC addresses ↔ named signals/terminals (the wiring table).
- **`jhiosimhostd.exe`** (42 KB) — a host daemon that hosts `iosim.dll` and exchanges the I/O
  block with the guest. It mirrors the same `_JHIOIntern*` calls. This is likely the transport
  used for **VMware** (which has no VirtualBox extension pack), while VirtualBox uses the JHIO
  extpack below.

## The JHIO VirtualBox extension pack (the bridge for VirtualBox)

File: `Heidenhain_VBoxJHIO_Extension_Pack-4.3.0-r6.vbox-extpack`. Contents:

```
ExtPack.xml: Name="Heidenhain VBoxJHIO Extension Pack"  Version=4.3.0 rev 6
             MainModule=VBoxJHIOMain
             Description="Provide VBoxJHIO host service for VirtualBox Heros5 guests."
win.amd64/VBoxJHIO.dll  (65 KB)   win.amd64/VBoxJHIOMain.dll  (20 KB)
win.x86/  VBoxJHIO.dll  (60 KB)   win.x86/  VBoxJHIOMain.dll  (19 KB)
ExtPack-license.txt:  proprietary; redistribution "strongly prohibited"
```

- `VBoxJHIOMain.dll` is the extension‑pack registration module (`VBoxExtPackRegister`); it
  checks a **minimum** VirtualBox version at load ("VirtualBox version >= %i.%i.0 required"),
  which is why a `4.3.0`‑labelled pack can load into VBox 7.1.4.
- `VBoxJHIO.dll` is the actual **HGCM host service** named `VBoxJHIO`. Its behaviour (from
  strings):
  - It uses the guest's **`IOsim` shared folder**: it finds the shared folder named `IOsim`,
    takes a **map‑file path** out of the JHIO header (a HeROS path), **translates it to the
    Windows host path** inside that shared folder, and **memory‑maps** it.
  - It then services guest HGCM calls by forwarding to `iosim.dll` (`JHIOsim.dll`): the same
    `_JHIOIntern*` block API, with per‑client connect/disconnect bookkeeping.
  - The header carries `JHDATASIZE`/`JHIO_HEADER` and a field `lvirtualTNCLicense`.
  - Synchronisation is per **PLC scan cycle** (`SignalPlcCycleDone` / `WaitForSimCycleDone`),
    i.e. the host I/O sim runs in lockstep with the guest PLC cycle.

So the data path is: **guest PLC ⇄ VBoxJHIO HGCM service ⇄ memory‑mapped file in `IOsim`
shared folder ⇄ `iosim.dll` (machine I/O model)**. This is the only host component that is
both essential to full machine behaviour *and* has no non‑Windows build *and* no source — the
prime reverse‑engineering target for a port.

## `tncvbinst.dll` (754 KB)

The MSI custom‑action DLL. Drives VirtualBox over COM to: import the OVA (`TNCvbCreateVmS`),
set shared folders / CPU / RAM / SVGA (`ModifyVm`), enumerate/delete VMs, create desktop
links, and install the JHIO extpack (`ExtPackInstall`). It is the programmatic equivalent of
a series of `VBoxManage import / modifyvm / sharedfolder add / extpack install` commands —
useful as a spec for reproducing the setup with `VBoxManage` on Linux.
