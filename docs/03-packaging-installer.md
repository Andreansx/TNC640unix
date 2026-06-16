# 03 — Packaging & Installer

## Top‑level folder map of the download

```
34059518SP4/
├─ ReadMe.txt / LiesMich.txt          install instructions (EN / DE): use TNCmanager → "Create Virtual Control"
├─ PGM-Platz_Virtual_en.pdf           main manual (99 pp, EN 02/2025, doc id 1369893)
├─ PGM-Platz_Virtual_de.pdf           main manual (DE)
├─ pgmplatz_tast_en.pdf / _de.pdf     2‑page key‑layout diagrams (refer back to main manual)
├─ 340595_18_SP4_Calls.pdf            2‑page fixed‑bug ticket list for 18 SP4 (no technical content)
├─ vTNC7_Programmiersystem_de.pdf     manual for the *successor* vTNC7 (817625‑20), DE — for cross‑reference
└─ 34059518/340595_18_SP4/Setup/
   ├─ autorun.inf                     open = "Install TNC640 (340595).exe"
   ├─ Install TNC640 (340595).exe     NSIS (Nullsoft) self‑extracting orchestrator (32‑bit)
   ├─ Uninstall TNC640 (340595).exe   NSIS uninstaller
   ├─ vbox/
   │  └─ VirtualBox-7.1.4-165100-Win.exe        (106 MB) the bundled hypervisor
   ├─ base/                           "TNCvbBase": hypervisor + VM + host control suite
   │  ├─ TNCvbBase.msi (36 MB) / TNCvbBase.exe  installs VBox, JHIO extpack, OVA, control suite
   │  ├─ TNCvbProg.ova (430 MB)                 the VM appliance (OVF + VMDK) — see §"The VM"
   │  ├─ Heidenhain_VBoxJHIO_Extension_Pack-4.3.0-r6.vbox-extpack   the PLC‑I/O host service (Win‑only)
   │  ├─ Cab001.cab / Cab002.cab                MSI payload
   │  ├─ LicenseAgreement.rtf / Lizenzbedingungen.rtf
   │  └─ misc/  vcredist_2010_x64 / 2013_x64 / VC_redist.x64.exe   required VC++ runtimes
   └─ prog/                           "TNCvbProg": the actual NC software image
      ├─ TNC640 (340595).exe          Advanced Installer bootstrapper (32‑bit) for the prog MSI
      ├─ TNCvbProg.msi (3.8 MB)       creates/installs/updates the VM from the OVA + setup.zip
      ├─ setup.zip (1.1 GB)           the HeROS5 + TNC640 software payload (see §"setup.zip")
      ├─ setup.cab (10 KB)
      └─ LicenseAgreement.rtf / Lizenzbedingungen.rtf
```

The two `*.exe` "installers" in `Setup/` and `prog/` are wrappers: the root one is **NSIS**;
`prog/TNC640 (340595).exe` is an **Advanced Installer (Caphyon)** bootstrapper (identifiable by
its `AI_*` properties). The real work is in the **two MSIs** and their custom‑action DLLs.

## The VM appliance — `TNCvbProg.ova`

A GNU‑tar archive containing `TNCvbProg.ovf` (VM definition XML) + `TNCvbProg-disk001.vmdk`
(stream‑optimized disk). The OVF is the single most useful artifact for a port; full values in
[reference/ovf-summary.md](reference/ovf-summary.md). Highlights:

- VM name **`HeROS5`**, `OSType=Linux_64`, machine UUID `{736e6e39-e51f-4178-9fb8-9776049f1284}`,
  config format `1.19-linux`.
- 1 vCPU / **1024 MB** RAM / 64 MB VRAM in the OVF, but the installer overrides this
  (`JhVmCpu=2`, RAM bumped — the manual budgets **3 GB per running station**).
- IDE **PIIX4** controller; one disk (virtual capacity **49,392,123,904 B ≈ 49.4 GB**, disk
  UUID `fc4ef3e5-…`). Chipset **ICH9**; IOAPIC, PAE, HW‑virt large pages; RTC=UTC.
- Display: `VRDE` (RDP) on **TCP 3389**; HID pointing = USBTablet; audio = ALSA.
- USB **OHCI** with 5 device filters (TE 7xx/6xx/5xx keyboards, MARX CrypToken, AKS Hardlock).
- Networking: adapter 0 = **Bridged** (`eno1`), adapter 1 = **Host‑Only** (`vboxnet0`),
  both Intel E1000; 34 more adapters declared but disabled.

### Disk partitioning (read from the actual VMDK)

The disk is MBR with one extended partition; full table in
[reference/partition-map.md](reference/partition-map.md):

| Partition | ext label | Size | Mount | Notes |
|---|---|---|---|---|
| p1 (primary, boot) | `HEROS5` | 20 GB | `/` | the OS root |
| p2 (primary) | `BOOT` | 200 MB | `/boot` | kernel + GRUB |
| p5 (logical) | `SYS` | 6 GB | `SYS:` | system / config (stays on disk) |
| p6 (logical) | `PLC` | 4 GB | `PLC:` | OEM PLC data (shared‑folder in VM mode) |
| p7 (logical) | `TNC` | 16 GB | `TNC:` | user NC programs (shared‑folder in VM mode) |

**On the base image, `SYS`/`PLC`/`TNC` are empty** (only `lost+found`). They are populated on
first install from `setup.zip`.

## `setup.zip` — the control‑software payload (1.1 GB)

This is the firmware/rootfs that gets flashed into the VM. 76 entries, including:

- **`target.tar.xz` (657 MB)** — the HeROS5 root filesystem (77,306 files, mostly `/usr`).
- **`SetupHeader.txt`** — the install manifest (see below).
- Partition/config images: `TNC640_SYS.*.zip`, `TNC640_TNC_Base.zip`,
  `TNC640_TNC_DefaultConfig.zip`, `TNC640_PLC_Base.zip`, `TNC640_PLC_DefaultConfig.zip`,
  `TNC640_*_Special.zip`.
- HeROS RPM packages (`*.i386.rpm` / `*.i586.rpm`): `opencascade-7.4.0` + `occdxf` (CAD/DXF
  kernel for the CAD‑Viewer/DXF import), `numpy`/`matplotlib`/`lapack`/`openblas`,
  `qupzilla` (browser), `heopcua` (OPC‑UA server), `jhdncif` (DNC interface), `eveusb`,
  `openvpn`, `pure-ftpd`, `hesra` (secure remote access), fonts. → confirms HeROS5 is a
  32‑bit‑userland, RPM‑based Linux.
- `*_vendorfileliste.txt` / `*_vendorCRCreference.txt` — per‑component file lists + CRCs used
  for integrity verification.

### `SetupHeader.txt` (the install logic, in plain text)

- `Product TNC640`, `NcIdentnummer 340595`, `NcVersion 18 SP4`, `MoveDestination SYS:\zip`.
- **BlackList** — the installer refuses to run when: host is `32BIT`, or the VM has
  `MEM<1536` (less than 1536 MB RAM).
- **PreSection / PostSection / PurgeHEROS** — shell commands executed *inside the guest*
  during install, operating on `/mnt/sys`, `/mnt/tnc`, `/mnt/plc`, `/home/user`
  (e.g. `chmod -R 775 /mnt/sys/*`, clean old logs/runtime files, set up `/mnt/plc/service`).
- A `SIGNATURE` line + CRC sums for tamper‑checking.
- The rest is a list of **SubContainers** (each RPM/zip with `CRC`, `Destination` =
  `HEROS:` / `SYS:` / …, size, file count).

### `setup.ini` (Create / Install / Update modes)

Extracted from the prog MSI; controls how the in‑VM updater applies the payload:

```
Create:  Interactive=NO  Confirm=NO  DelSource=YES  DoInstall=TNC640_PLC_DefaultConfig.*zip ; TNC640_TNC_DefaultConfig.*zip
Install: Interactive=YES Confirm=YES DelSource=YES  DoInstall=…DefaultConfig…   (applies default machine config)
Update:  Interactive=YES Confirm=YES DelSource=YES                              (system update only)
```

So a fresh **Create/Install** also applies the **default PLC + TNC machine configuration**
(kinematics, machine parameters), while **Update** only refreshes system software.

## Install flow & resulting on‑disk layout

The base MSI (`TNCvbBase`) and prog MSI (`TNCvbProg`) custom actions (`tncvbinst.dll`,
`aicustact.dll`) do, in order:

1. Require **VT‑x/AMD‑V** and **VirtualBox *or* VMware (VIX)**; install VC++ redists.
2. Install **VirtualBox 7.1.4**; register the **JHIO extension pack**
   (`ExtPackInstall …Heidenhain_VBoxJHIO…vbox-extpack`).
3. Lay down the **host control suite** under `…\HEIDENHAIN\TNCvbBase\control\`.
4. **Import the OVA** as a new VM (`TNCvbCreateVmS`), set CPU/RAM/SVGA, and attach **shared
   folders** `Install`, `IOsim` (auto‑mount) and `PLC`, `TNC` (`*noautomount`).
5. Create the desktop/Start‑menu shortcut: `tncvbcntl.exe "<VM>.vbox"`.
6. On first VM start, the guest unpacks/installs `setup.zip` into `SYS`/`PLC`/`TNC` and boots
   the control.

Resulting host directory tree (Windows):

```
C:\Program Files\HEIDENHAIN\
├─ TNCvbBase\
│  ├─ control\  tncvbcntl.exe  keypad.exe  handwheel.exe  jhiosimhostd.exe
│  │           iosim.dll  plcmap.dll  (+ win32\ 32‑bit variants)  + Qt6 runtime
│  └─ install\  tncvbinst.dll
└─ TNC640\340595\<VM name>\
   ├─ <VM name>.vbox            VirtualBox machine config
   ├─ TNCvbProg-disk001.vmdk    the disk
   ├─ Install\                  shared folder → guest /mnt/sf/Install  (setup payload)
   ├─ IOsim\                    shared folder → guest /mnt/sf/IOsim    (JHIO I/O map file)
   ├─ PLC\                      shared folder → guest PLC:  (OEM PLC data; noautomount)
   ├─ TNC\                      shared folder → guest TNC:  (USER NC programs; noautomount)
   └─ Snapshots\
```

The **`TNC\` host folder is where your NC programs live** — that's the simplest file‑exchange
path between the PC and the control (see [07](07-networking-licensing.md)).
