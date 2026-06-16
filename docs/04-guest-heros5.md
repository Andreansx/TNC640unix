# 04 — Guest: HeROS5 Internals

Everything here was read **read‑only** from the VMDK (converted to a raw image and inspected
with `debugfs`; see [10 — Methodology](10-methodology.md)). The base image was *not* booted or
modified.

## OS identity

```
/etc/heros-release:
  HEROS_MAJOR_VERSION=5   HEROS_MINOR_VERSION=18   HEROS_PATCH_VERSION="04.002"
  HEROS_VERSION=5.18.04.002   HEROS_VERSION_DATE="08:34 UTC Wed 07.05.2025"
  HEROS_YOCTO_HASH=8f402999178b1a2a3a9a8e8f1f3cf3c8fb7727be
/etc/issue.net: HEROS Linux (HEIDENHAIN Realtime Operating System) 5.18.04.002
/etc/version:   20180309123456
```

**HeROS5 is a Yocto/OpenEmbedded‑built, RPM‑packaged, real‑time‑oriented x86‑64 Linux.**
(HeROS5 = the generation used by NC‑SW 18; newer controls use HeROS6.) Userland packages are
32‑bit (`i386`/`i586`) on a 64‑bit kernel. Desktop = **XFCE**; display manager = **SDDM**;
window flavor includes `matchbox`, `plymouth` splash.

## Filesystems & partitions

| Dev (in raw image) | Label | Role |
|---|---|---|
| p1 | `HEROS5` | root `/` (ext4, 20 GB) |
| p2 | `BOOT` | `/boot` (200 MB) |
| p5 | `SYS` | `SYS:` system/config (6 GB) |
| p6 | `PLC` | `PLC:` OEM PLC data (4 GB) |
| p7 | `TNC` | `TNC:` user NC programs (16 GB) |

`/etc/fstab` is minimal (only `devpts`, `shm`, `cgroup2`). The NC volumes are mounted
**programmatically by init** from `/etc/sysconfig/partitions.*.cfg`, and the hypervisor type
decides which config wins:

- **`partitions.ext3ci.cfg`** (real HW / disk mode): `SYS:`→`/dev/sda5`, `PLC:`→`/dev/sda6`,
  `TNC:`→`/dev/sda7`, all `ext3ci` (a **case‑insensitive ext3** variant — needed for the
  control's DOS‑style 8.3‑ish file naming).
- **`partitions.vboxsf.cfg`** (VirtualBox VM mode): `PLC:` and `TNC:` are mounted as
  **vboxsf shared folders** (`DEVICE=PLC` / `DEVICE=TNC`, `JHVI=net`, `MANDATORY=1`), so they
  come from the **host** filesystem. `SYS:` stays the on‑disk partition.
- Also present: `partitions.vmhgfs.cfg` (VMware shared folders), `partitions.vfat.cfg`,
  `partitions.btrfs.cfg`. HeROS auto‑adapts to its environment.

The volume abstraction (`TNC:`, `SYS:`, `PLC:`, `SF:`, `LOG:`) is managed by a HeROS tool
`jhvolume`.

## Boot sequence

- **SysVinit** (`/etc/inittab` → `/etc/init.d/rc <runlevel>`), classic `rc0.d…rc6.d`, plus
  some systemd bits and a `runonce` mechanism (`/etc/runonce/ro_*` scripts run once after an
  install/update — e.g. `ro_ext3ci`, `ro_adjust_config_paths`, `ro_rpminstall`).
- **SDDM** with **autologin** (`/etc/sddm.conf`): `User=autologin`, session `startxfce4`,
  theme `jh-theme`. The X server is started with `-listen tcp`.
- XFCE autostarts the control MMI fullscreen (the MMI binaries come from `target.tar.xz`, not
  the base image).

### Notable `/etc/init.d` scripts (the interesting ones)

| Script | Purpose |
|---|---|
| `virtualbox` | **Host integration** — detect VBox, load Guest Additions, mount shared folders, set `/HEIDENHAIN/*` guest properties. Full analysis in [06](06-bridge-and-io.md). |
| `vmware` | the VMware equivalent of the above |
| `heuinput` | **synthetic keyboard input** daemon (the keypad's target) — see [06](06-bridge-and-io.md) |
| `jhloadkbd` | load the HEIDENHAIN keyboard mapping (TE keyboards) |
| `vnc` | runs `/usr/sbin/hevnc` (x11vnc) — remote screen for service/`hesra`, not the local display |
| `applaunch` / `appmount` / `checkappmounterr` | launch & mount the NC application partitions |
| `heros` | main HeROS service |
| `xstart` / `xclient` / `xfcestart` / `xserver-nodm` | X / desktop startup |
| `ncpath-functions` / `ncpoweroff` / `ncsmb` | NC‑specific helpers (paths, safe power‑off, SMB) |
| `eveusb` | USB licensing‑dongle daemon |
| `watchdog` | system watchdog |

## Kernel modules (`/etc/sysconfig/modules`)

Standard: `tun, e100, e1000e, e1000, pch_gbe, igb, psmouse, nbd, fuse, rtc-cmos, hwmon,
i2c-*, w83781d, adt7475, input-polldev, msr, cdrom, sr_mod, af_packet, des_generic, 8021q`.

**HEIDENHAIN real‑time / NC modules:** `jhnc` (NC kernel), `hsci` (HSCI = HEIDENHAIN Serial
Controller Interface — the realtime fieldbus to drives/I/O on real machines), `hik`, `jhrio`
(remote I/O), `jhint` (interrupt), `fe1`, `fex`, `hldx`, `heros`. On a real machine these
talk to motor controllers and I/O terminals over HSCI; **in the VM there is no real fieldbus**,
so the machine's I/O is provided by the **JHIO PLC‑I/O simulation** instead (see
[06](06-bridge-and-io.md)). In the VM, the standard VirtualBox Guest Additions modules
(`vboxguest`, `vboxvideo`, `vboxsf`) are loaded by the `virtualbox` init script.

## Users (`/etc/passwd`)

| User | uid | Purpose |
|---|---|---|
| `root` | 0 | super user |
| `sys` | 1 | NC system function user |
| `oem` | 20 | **machine‑tool‑builder** account (OEM/PLC data) |
| `user` | 500 | the **operator** (default login; TNCremo creds `user`/`user` when user‑mgmt is off) |
| `autologin` | 400 | auto‑login account that brings up the control GUI when nobody is logged in |
| `useradmin` | 401 | built‑in user administrator |
| `vmusr` | 19 | VMware/VM remote‑login function user (gets a random password set via guest property) |

User administration can be backed by **SSSD / Kerberos / OpenLDAP** (HeROS supports domain
join); locally it's the simple accounts above.

## Userspace tooling (HeROS `he*` / `jh*` programs, from `target.tar.xz`)

A large suite of HEIDENHAIN tools under `/usr/bin` and `/usr/sbin`, including:

- **GUI / input:** `hepanel` (control panel surface), `heoskeyboard` (on‑screen keyboard),
  `hesoftkeysqt` (soft‑key display server — pairs with the host keypad), `heuinput`.
- **Licensing:** `hegetsikopt` (read SIK option bits), `helicenseviewer`.
- **Network/admin:** `henetsetupqt`, `heclientssh`, `hemasterssh`, `hecertadmin`/`hepkiadmin`
  (PKI), `heuseradmin`/`heoemuseradmin`, `hefwconfig-cmd` (firewall), `heopcuaconfig`.
- **Logging/service:** `helogger`/`helogexplorer`/`helogflush`, `hemkservicefile`,
  `hebackup`/`herestore`, `hecrypt`/`hecryptozip_cli`.

These confirm the SIK licensing model, the on‑screen keyboard/soft‑key servers that the host
keypad drives, and that HeROS is a full managed Linux platform (PKI, OPC‑UA, domain auth,
VPN, FTP) rather than a thin appliance.
