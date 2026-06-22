# 06 — Host↔Guest Bridge & I/O

This is the heart of "how the standalone .exe communicates with the VM." There are **five**
distinct channels. None is exotic — all are standard VirtualBox features plus a small custom
service — which is good news for a port.

## 1. Hypervisor detection (guest side)

`/etc/init.d/virtualbox` decides whether HeROS is inside VirtualBox by looking for the
VirtualBox guest PCI device:

```sh
if ! cat /proc/bus/pci/devices | grep -q "80eecafe" ; then  # not a VirtualBox VM → bail
```

`80eecafe` = VirtualBox's signature PCI ID (`80ee:cafe`). If found, it loads the **Guest
Additions** kernel modules `vboxguest`, `vboxvideo`, `vboxsf` and proceeds. (The VMware path
is the analogous `/etc/init.d/vmware`.) For a Linux/VirtualBox host this all works unchanged;
the Guest Additions are already built into the HeROS image.

## 2. Shared folders (the data plane)

`/etc/init.d/virtualbox` creates `/mnt/sf` and mounts the host‑provided shared folders. The
auto‑mounted ones are:

```sh
mount -t vboxsf -o uid=500,gid=100 Install /mnt/sf/Install
mount -t vboxsf -o uid=500,gid=100 IOsim   /mnt/sf/IOsim
jhvolume --set 'SF:' /mnt/sf -s=network -a=user -i=hdd
ln -s ../../mnt/sf /home/oem/sf ; ln -s ../../mnt/sf /home/user/sf
```

and `/etc/sysconfig/partitions.vboxsf.cfg` additionally mounts `PLC` and `TNC` as the
control's `PLC:` and `TNC:` volumes. So the four shared folders are:

| Shared folder | Guest mount | Purpose |
|---|---|---|
| `Install` | `/mnt/sf/Install` (`SF:`) | the `setup.zip` payload, presented to the guest's updater on first start |
| `IOsim` | `/mnt/sf/IOsim` | holds the **memory‑mapped JHIO I/O file** (PLC I/O block) |
| `PLC` | `PLC:` | OEM PLC data (machine builder) — mounted `*noautomount`, brought up by config |
| `TNC` | `TNC:` | **user NC programs** — this is where your `.H`/`.I` programs live on the host |

Because `PLC:`/`TNC:` are host folders, the host can read/write NC programs directly, and the
control sees them live. This is the simplest file‑exchange mechanism.

## 3. Guest properties (the control plane)

VirtualBox guest properties under **`/HEIDENHAIN/*`** are a bidirectional key/value channel
between host and guest (`VBoxControl guestproperty get/set` in the guest; the host sets them
via COM/`VBoxManage guestproperty`). Observed keys:

| Key | Direction | Meaning |
|---|---|---|
| `/HEIDENHAIN/VMUSER/PW` | guest→host | guest generates a random password for the `vmusr` account and publishes it so the host can log in for file/remote access; invalidated on shutdown |
| `/HEIDENHAIN/CMD/Cmd` | host→guest | command channel (e.g. `restart`); guest acts and clears it |
| `/HEIDENHAIN/LC_ALL` | host→guest | locale to apply (`hesetlocale`) |
| `/HEIDENHAIN/CFG/Display/VMSVGA` | host↔guest | VRAM/SVGA mode (e.g. `64`); mismatch triggers a guest restart |
| `/HEIDENHAIN/CFG/Display/{Mode,Accel3d,LastResolution,LastVMSVGA}` | host→guest | display configuration from the Control Panel's Display tab |

`/etc/init.d/virtualbox` also runs `VBoxService`, sets the guest hostname from a guest
property (`networkcheck setvhostname`), and toggles a `00vboxvideo.conf` ld.so hack for 3D
acceleration.

## 4. Synthetic keyboard input — `heuinput` (host keypad → guest)

`/etc/init.d/heuinput` runs a daemon that injects synthetic keystrokes via the Linux
**`uinput`** subsystem:

```sh
HEUINPUTPIPE="/tmp/__heuinput";  HEUINPUT="/usr/bin/heuinput"
mkfifo $HEUINPUTPIPE ; chmod 0720 $HEUINPUTPIPE ; chown root:syninput $HEUINPUTPIPE
modprobe uinput
/usr/bin/heuinput < $HEUINPUTPIPE &     # reads commands from the FIFO, emits uinput key events
```

So anything written to the FIFO `/tmp/__heuinput` (by a member of group `syninput`) becomes a
keypress in the guest's X session — which is exactly how **`keypad.exe`** turns a clicked
soft key into a control keystroke. The host keypad must therefore reach this FIFO; how the
Windows keypad does that across the VM boundary (guest control vs. a network forwarder) is the
main thing to confirm empirically. A Linux substitute can be as simple as a small on‑screen
keyboard that `echo`s the right tokens into `/tmp/__heuinput` over SSH/guest‑control.

## 5. Handwheel — TCP 19035 (host handwheel → guest)

`handwheel.exe` opens a `QTcpSocket` to **port 19035** on the guest (over the host‑only
network). The handwheel server is in the **NCK interpolator `ipo.elf`** (`HRSimServer.cpp`); it
consumes jog pulses, the selected axis, and feed/override. **The wire format is now reverse‑
engineered** from the server: a **33‑byte input frame** (8×int32 LE + 1 byte) per client (up to
5), and an LED‑bitmap + HR 520/550 display reply — see
[18-handwheel-and-jhio-network.md](18-handwheel-and-jhio-network.md) and the native
`handwheel/` codec.

## 6. JHIO — PLC I/O simulation (the realtime channel)

The deepest integration. On a real machine the PLC drives physical I/O over HSCI; in the VM
that I/O is simulated on the host and exchanged with the guest PLC **every PLC scan cycle**:

```
guest PLC  ──HGCM calls──►  VBoxJHIO host service (extpack)
                               │  reads map‑file path from JHIO header,
                               │  translates HeROS path → host path under the IOsim shared folder,
                               ▼  memory‑maps that file
                          shared "IOsim" file  (JHIO_HEADER + JHDATASIZE data block)
                               ▲
                               │  iosim.dll reads/writes named signals (JHIOGet/SetByteValue, …)
                          iosim.dll + plcmap.dll  (the machine I/O model)
   sync:  guest SignalPlcCycleDone ⇄ host WaitForSimCycleDone   (lockstep per scan cycle)
```

- **Transport options:** (1) VirtualBox uses the **JHIO extension pack** (`VBoxJHIO` HGCM
  service); (2) VMware uses the standalone **`jhiosimhostd.exe`** polling the shared file; (3) **a
  network transport built into the guest** — `usr/lib/libjhiosimnet.so.1.0` (linked by `plc.elf`)
  serves the *same* `_JHIOIntern*` block API over **TCP 19009** (guest = server on the machine‑net
  eth interface; host connects as client). This third path is **fully cross‑platform** and is the
  recommended route for a UNIX/macOS host — it needs no Windows extpack. Decoded in
  [18-handwheel-and-jhio-network.md](18-handwheel-and-jhio-network.md) (config vars
  `JHIOSIM_MODE/GUEST_IF/SVR_IP/SVR_PORT`, the 740‑byte `JHIO_HEADER`, the per‑cycle exchange).
  All three ultimately drive a machine I/O model (`iosim.dll`+`plcmap.dll` on Windows).
- **Why it matters:** without a working I/O sim the guest PLC may not reach the operational
  ("control ready") state — the machine model would look "not ready," errors could latch, and
  axis motion / M‑functions might be blocked. Whether *demo programming + simulation* tolerates
  its absence is an open question worth testing first (it's plausible that pure NC‑program
  editing + toolpath simulation works without it, while "machine operating modes" do not).

## Display (for completeness)

- **Local:** the **native VirtualBox VM window** showing the guest's fullscreen XFCE+MMI,
  configured by `tncvbcntl.exe` via `GUI/*` extradata. This is what you see.
- **Remote:** the OVF also configures **VRDE on TCP 3389** (RDP) and the guest runs
  `hevnc`/x11vnc — for remote service access (HEIDENHAIN "RemoteAccess"/`hesra`) and the
  classroom/instructor scenario. Note: VBox 7.x VRDE needs **Oracle's** extension pack, which
  is **not** bundled here (only the JHIO one is) — so a headless‑remote display path needs that
  added, or use VNC.

## Summary table — integration points for a port

| Channel | Mechanism | Concrete handle |
|---|---|---|
| hypervisor detect | PCI id | `80ee:cafe` |
| files / NC programs | VBox shared folders | `Install`, `IOsim`, `PLC`, `TNC` |
| control plane | VBox guest properties | `/HEIDENHAIN/*` |
| keyboard input | guest `uinput` daemon | FIFO `/tmp/__heuinput`, group `syninput` |
| handwheel | TCP socket | guest port **19035** |
| PLC I/O sim | HGCM + mmap file | `IOsim` shared file ⇄ `iosim.dll`, per‑cycle sync |
| screen (local) | native VBox window | `GUI/*` extradata, fullscreen |
| screen (remote) | VRDE / VNC | TCP **3389** (needs Oracle extpack) / `hevnc` |
