#!/bin/bash
# Import HEIDENHAIN's TNCvbProg.ova and wire it up like the TNCvbProg installer does, so the
# TNC 640 control boots on an x86-64 Linux host under VirtualBox. See docs/11-running-on-linux.md.
#
# Prereqs: VirtualBox 7.1 installed + vboxdrv loaded; the proprietary package present under
# <repo>/34059518/340595_18_SP4/Setup (NOT shipped in this repo). Run from anywhere.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SETUP="$REPO/34059518/340595_18_SP4/Setup"
OVA="$SETUP/base/TNCvbProg.ova"
VM="${VM:-TNC640}"
BASE="$REPO/work/vbox"
VMDIR="$REPO/work/vm"

[ -f "$OVA" ] || { echo "ERROR: OVA not found at $OVA (supply the HEIDENHAIN package)"; exit 1; }
mkdir -p "$BASE" "$VMDIR"/{Install,IOsim,PLC,TNC}

# clean any prior instance
VBoxManage controlvm "$VM" poweroff >/dev/null 2>&1 || true
VBoxManage unregistervm "$VM" --delete >/dev/null 2>&1 || true

VBoxManage setproperty machinefolder "$BASE"
echo "=== importing OVA ==="
VBoxManage import "$OVA" --vsys 0 --vmname "$VM"

echo "=== hardware ==="
VBoxManage modifyvm "$VM" --cpus 2 --memory 3072 --vram 64 --graphicscontroller vmsvga --pae on --ioapic on
VBoxManage modifyvm "$VM" --nic1 nat
for i in 2 3 4 5 6 7 8; do VBoxManage modifyvm "$VM" --nic$i none 2>/dev/null || true; done

echo "=== shared folders ==="
VBoxManage sharedfolder add "$VM" --name Install --hostpath "$VMDIR/Install" --automount
VBoxManage sharedfolder add "$VM" --name IOsim   --hostpath "$VMDIR/IOsim"   --automount
VBoxManage sharedfolder add "$VM" --name PLC     --hostpath "$VMDIR/PLC"
VBoxManage sharedfolder add "$VM" --name TNC     --hostpath "$VMDIR/TNC"
VBoxManage guestproperty set "$VM" /HEIDENHAIN/CFG/Display/VMSVGA 64

echo "=== stage NC software for first-boot flashing ==="
ln -f "$SETUP/prog/setup.zip" "$VMDIR/Install/setup.zip" 2>/dev/null || cp "$SETUP/prog/setup.zip" "$VMDIR/Install/setup.zip"
chmod 0755 "$VMDIR/Install/setup.zip"
printf 'Interactive=NO\nConfirm=NO\nDelSource=YES\nDoInstall=TNC640_PLC_DefaultConfig.*zip\nDoInstall=TNC640_TNC_DefaultConfig.*zip\n' > "$VMDIR/Install/setup.ini"

echo "=== done. Start with:  VBoxManage startvm $VM --type headless ==="
echo "=== capture screen:    VBoxManage controlvm $VM screenshotpng /tmp/shot.png ==="
