#!/bin/bash
# Import HEIDENHAIN's TNCvbProg.ova on yeen (Arch x86-64 + VirtualBox) and wire it up like the
# Windows installer, so the TNC 640 control boots and first-boot-flashes the NC software.
# Standalone (no repo clone needed): paths under ~/tnc. See docs/11-running-on-linux.md.
# Run AFTER VirtualBox is installed (virtualbox + virtualbox-host-modules-arch, vboxdrv loaded,
# user in vboxusers) and the OVA + setup.zip are present under ~/tnc/Setup.
set -euo pipefail

TNC="$HOME/tnc"
SETUP="$TNC/Setup"
OVA="$SETUP/base/TNCvbProg.ova"
VM="${VM:-TNC640}"
BASE="$TNC/vbox"
VMDIR="$TNC/vm"

command -v VBoxManage >/dev/null || { echo "ERROR: VBoxManage not found (install virtualbox)"; exit 1; }
[ -f "$OVA" ] || { echo "ERROR: OVA not found at $OVA"; exit 1; }
[ -f "$SETUP/prog/setup.zip" ] || { echo "ERROR: setup.zip not found at $SETUP/prog/setup.zip"; exit 1; }
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

echo "=== NAT port-forwards (reach guest services from the host) ==="
# host 127.0.0.1:<p> -> guest:<p>.  19035 handwheel (ipo.elf), 19009 JHIO (plc.elf),
# 5900 VNC (x11vnc, screen), 2222->22 ssh into guest.
for rule in "handwheel,tcp,127.0.0.1,19035,,19035" \
            "jhio,tcp,127.0.0.1,19009,,19009" \
            "vnc,tcp,127.0.0.1,5900,,5900" \
            "gssh,tcp,127.0.0.1,2222,,22"; do
  VBoxManage modifyvm "$VM" --natpf1 "$rule" 2>/dev/null || true
done

echo "=== shared folders ==="
VBoxManage sharedfolder add "$VM" --name Install --hostpath "$VMDIR/Install" --automount
VBoxManage sharedfolder add "$VM" --name IOsim   --hostpath "$VMDIR/IOsim"   --automount
VBoxManage sharedfolder add "$VM" --name PLC     --hostpath "$VMDIR/PLC"
VBoxManage sharedfolder add "$VM" --name TNC     --hostpath "$VMDIR/TNC"
VBoxManage guestproperty set "$VM" /HEIDENHAIN/CFG/Display/VMSVGA 64

echo "=== stage NC software for first-boot flashing ==="
cp -f "$SETUP/prog/setup.zip" "$VMDIR/Install/setup.zip"
chmod 0755 "$VMDIR/Install/setup.zip"
printf 'Interactive=NO\nConfirm=NO\nDelSource=YES\nDoInstall=TNC640_PLC_DefaultConfig.*zip\nDoInstall=TNC640_TNC_DefaultConfig.*zip\n' > "$VMDIR/Install/setup.ini"

echo "=== done. Start:  VBoxManage startvm $VM --type headless ==="
echo "=== screenshot:   VBoxManage controlvm $VM screenshotpng /tmp/shot.png ==="
echo "=== guest IP (after boot, for 19035/19009):  VBoxManage guestproperty get $VM /VirtualBox/GuestInfo/Net/0/V4/IP ==="
