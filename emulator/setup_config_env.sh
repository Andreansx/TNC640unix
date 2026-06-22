#!/bin/sh
# setup_config_env.sh — set up the HeROS config environment so ConfigServer can (begin to) load
# its config: the volume DB, the /mnt/sys mount paths, and encfs. Run once in the lima VM (needs
# root):  sh emulator/setup_config_env.sh
#
# This implements blocker-#6 environment steps (encfs install, writable /mnt/sys/config, colon-form
# volume DB). IMPORTANT FINDING — these are NECESSARY-LAYERS but NOT SUFFICIENT:
#   * Volume resolution works (colon-form names "SYS:" -> /mnt/sys); jhconfigfiles.cfg IS read+parsed.
#   * encfs (1.9.5) installs and /mnt/sys/config is writable, but the control's `encdir` still fails
#     to encfs-mount jh_int under qemu (error encfs / error unshare — FUSE+namespaces). That subdir is
#     OEM-secret storage (jh_int); the channel config (tnc.cfg) is PLAINTEXT, so encfs is a red herring.
#   * THE REAL GATE: host-strace proves ConfigServer NEVER opens/stats tnc.cfg (or any data .cfg/.atr) —
#     so CfgStore per-layer registration is empty (CntDataFiles=0) and ReadDataFiles skips every file.
#     The config it does broadcast (a 4380B QEvtServer payload) comes from a cache (/tmp/CBIOS_MAPPED_
#     FILE_REV_200), not the files. The registration is likely gated on the runtime productid cache
#     (/mnt/sys/cache/nckern/productid/*.conf — absent, ENOENT) which selects the config variant/layer.
#   NEXT: RE ServerHelper::SetupDirInfo@0x2a2a60 / ReadConfigDataDir@0x2150a0 — why CfgStore::DataFile
#   registers nothing; provide/generate the productid cache; or seed the CBIOS config cache. This is
#   the multi-component config subsystem (the documented FUSE-backends/config frontier), not one fix.
set -e
CTRL=${CTRL:-/Users/andreansx/Documents/TNC640unix/work/control/sysroot}
RT=${RT:-/Users/andreansx/Documents/TNC640unix/work/target/rootfs}

echo "== install encfs + fuse =="
sudo apt-get install -y -qq encfs fuse 2>/dev/null || true
encfs --version 2>&1 | head -1 || true

echo "== writable /mnt/sys (config copied writable, rest symlinked to the sysroot) =="
sudo rm -rf /mnt/sys; sudo mkdir -p /mnt/sys
for d in "$CTRL"/*; do n=$(basename "$d"); [ "$n" = config ] && continue; sudo ln -sfn "$d" "/mnt/sys/$n"; done
sudo cp -r "$CTRL/config" /mnt/sys/config
sudo chown -R "$(id -u):$(id -g)" /mnt/sys/config
sudo ln -sfn "$CTRL/default/oem" /mnt/plc
sudo ln -sfn "$CTRL/default/oem" /mnt/plce
sudo ln -sfn "$CTRL" /mnt/tnc

echo "== /etc/jhvolume — colon-form volume names pointing at the /mnt mount paths =="
jhv(){ sudo qemu-i386 -L "$RT" -E LD_LIBRARY_PATH=/heros5/bin:/lib:/usr/lib \
        "$RT/lib/ld-linux.so.2" "$RT/usr/sbin/jhvolume" "$@" >/dev/null 2>&1; }
sudo rm -f /etc/jhvolume
jhv --set "SYS:"    /mnt/sys -a=sys
jhv --set "SYSTEM:" /mnt/sys -a=sys
jhv --set "PLC:"    /mnt/plc -a=oem
jhv --set "PLCE:"   /mnt/plce -a=oem
jhv --set "OEM:"    /mnt/plc -a=oem
jhv --set "TNC:"    /mnt/tnc -a=user
echo "== done. /etc/jhvolume: =="; sudo cat /etc/jhvolume
