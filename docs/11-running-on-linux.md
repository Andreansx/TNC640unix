# 11 — Running the Control on x86-64 Linux (verified)

This is a **reproducible, tested procedure** that boots the real TNC 640 control (NC SW 340595,
18 SP4) on an x86-64 Linux host under VirtualBox, fully **headless**, and reaches the live MMI in
demo mode. Verified 2026-06-16 on an Ubuntu 24.04 x86-64 VPS (kernel 6.17, 2 vCPU, 15 GB RAM,
nested VT-x via `/dev/kvm`).

> Proprietary files (`34059518/…`, the OVA, `setup.zip`) are **not** in this repo — supply your
> own legitimately obtained copy. See [09-legal.md](09-legal.md).

> **Just want to run it?** Use the launcher: `./tnc640 setup` then `./tnc640 run` — it automates
> everything below. See the user manual, [14-install-and-run.md](14-install-and-run.md). This page
> is the underlying, hand‑verified procedure (what the launcher does and why).

## Why VirtualBox (not plain QEMU/KVM)

The guest's `/etc/init.d/applaunch` only auto-installs `setup.zip` when it detects a supported
hypervisor: `check_vbox()` runs `VBoxControl version` and, on success, sets
`JH_VIRTUALIZATION=VBOX`; the install step is then gated on that being set. Plain QEMU/KVM is not
detected (no VirtualBox guest PCI device `80ee:cafe`), so HeROS boots but never installs the NC
software. Hence: **VirtualBox** (or VMware). VirtualBox also provides the `vboxsf` shared folders
and guest properties the product relies on.

## Prerequisites

```bash
# Host must expose hardware virtualization (VT-x/AMD-V). On a VPS this means nested virt:
egrep -c '(vmx|svm)' /proc/cpuinfo      # must be > 0
ls -l /dev/kvm                          # present

# Oracle VirtualBox 7.1.x (matches the product's 7.1.4; builds cleanly on modern kernels)
curl -fsSL https://www.virtualbox.org/download/oracle_vbox_2016.asc \
  | sudo gpg --dearmor -o /usr/share/keyrings/oracle-vbox-2016.gpg
. /etc/os-release
echo "deb [arch=amd64 signed-by=/usr/share/keyrings/oracle-vbox-2016.gpg] \
https://download.virtualbox.org/virtualbox/debian $VERSION_CODENAME contrib" \
  | sudo tee /etc/apt/sources.list.d/virtualbox.list
sudo apt-get update
sudo apt-get install -y build-essential "linux-headers-$(uname -r)" virtualbox-7.1
sudo usermod -aG kvm "$USER"            # log out/in, or wrap VBox commands in: sg kvm -c '…'

# verify the kernel module is loaded
lsmod | grep -q vboxdrv && echo "vboxdrv OK"
```

## Import and wire up the VM (exactly like the installer)

```bash
ROOT=$PWD                                   # repo root
SETUP="$ROOT/34059518/340595_18_SP4/Setup"
VM=TNC640
BASE="$ROOT/work/vbox"; mkdir -p "$BASE"

VBoxManage setproperty machinefolder "$BASE"
VBoxManage import "$SETUP/base/TNCvbProg.ova" --vsys 0 --vmname "$VM"
VBoxManage modifyvm "$VM" --cpus 2 --memory 3072 --vram 64 --graphicscontroller vmsvga --pae on --ioapic on
VBoxManage modifyvm "$VM" --nic1 nat        # OVF's bridged eno1 / hostonly vboxnet0 won't exist here
for i in 2 3 4 5 6 7 8; do VBoxManage modifyvm "$VM" --nic$i none; done

# shared folders the control expects (host dirs under work/vm)
mkdir -p "$ROOT/work/vm/"{Install,IOsim,PLC,TNC}
VBoxManage sharedfolder add "$VM" --name Install --hostpath "$ROOT/work/vm/Install" --automount
VBoxManage sharedfolder add "$VM" --name IOsim   --hostpath "$ROOT/work/vm/IOsim"   --automount
VBoxManage sharedfolder add "$VM" --name PLC     --hostpath "$ROOT/work/vm/PLC"
VBoxManage sharedfolder add "$VM" --name TNC     --hostpath "$ROOT/work/vm/TNC"
VBoxManage guestproperty set "$VM" /HEIDENHAIN/CFG/Display/VMSVGA 64
```

(`scripts/setup_vm.sh` in this repo automates all of the above, including staging the NC software.)

## Stage the NC software for first-boot flashing

`applaunch` looks in the mounted `Install` share for `setup.zip` (must be **executable**) plus a
matching `setup.ini`:

```bash
ln -f "$SETUP/prog/setup.zip" "$ROOT/work/vm/Install/setup.zip"   # same fs → no 1.1 GB copy
chmod 0755 "$ROOT/work/vm/Install/setup.zip"
printf 'Interactive=NO\nConfirm=NO\nDelSource=YES\nDoInstall=TNC640_PLC_DefaultConfig.*zip\nDoInstall=TNC640_TNC_DefaultConfig.*zip\n' \
  > "$ROOT/work/vm/Install/setup.ini"
```

`DelSource=YES` makes the installer delete `setup.zip` after a successful flash, so the next boot
launches the control instead of reinstalling. (If a run fails, re-create the file to retry.)

## Boot headless and watch it

```bash
VBoxManage startvm "$VM" --type headless
# capture the screen at any time (no VRDE/Oracle-extpack needed):
VBoxManage controlvm "$VM" screenshotpng /tmp/shot.png
```

First boot sequence (≈15 min on 2 vCPU):
1. HEROS boot splash → first-boot `runonce` provisioning.
2. **TNC640 Software Update**: Copy setup → Extract archive (657 MB) → Prepare RPM packages →
   Replace files → Install special files → Finalize. Then it deletes `setup.zip` and reboots.
3. Post-install boot: "Installing RPM software packages" → desktop services → **the TNC 640 MMI**.
4. A **"Shareware"** dialog appears: *demo version, max 100 NC lines* — this is the unlicensed
   demo mode (no dongle needed). A *"Default OEM passwords detected"* HeROS notice also appears.

## Driving the headless MMI (keyboard injection)

`VBoxManage controlvm "$VM" keyboardputscancode <hex make> <hex break>` reaches the MMI's X
session. **Verified mapping: the 8 horizontal soft keys = F1..F8** (scancodes `3b..42`).

```bash
# acknowledge the demo "Shareware" dialog (its OK is the left soft key = F1):
VBoxManage controlvm "$VM" keyboardputscancode 3b bb
```

After that you reach the control in **Programming** mode; a cold start shows **"Power
interrupted"** and the control-voltage-ON soft key (normal TNC startup).

## Status / limits reached so far

- ✅ Control installs and boots to the live MMI, **demo mode**, headless, on x86-64 Linux.
- ✅ Soft keys respond to injected keypresses (F1..F8).
- ⏳ **Full input mapping** (the complete PC-key → TNC-key set, incl. CE/ENT/axis keys) is not yet
  worked out here — the faithful source is the on-screen `keypad.exe` feeding the guest `heuinput`
  FIFO; on Linux this needs either the documented keymap or a small substitute. See
  [06-bridge-and-io.md](06-bridge-and-io.md).
- ⏳ **Machine power-on / operating modes** likely need the **JHIO PLC-I/O simulation** (Windows-
  only today). Pure NC programming + simulation in Programming mode is expected to work without it;
  to be confirmed.
- ℹ️ Screen is the native VBox framebuffer captured via `screenshotpng`. For interactive viewing,
  enable VRDE (needs Oracle's extension pack) or run a VNC into the guest.
