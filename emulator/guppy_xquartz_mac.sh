#!/bin/bash
# guppy_xquartz_mac.sh — surface the FEX-native Guppy GTK window as a NATIVE macOS window via
# XQuartz (rootless, NO VNC). Runs ON THE MAC. Sets up a reverse SSH tunnel from the lima VM `tnc`
# to the Mac's XQuartz X server (port 6000) and launches Guppy with DISPLAY=localhost:0.
#
# Prereq (ONE-TIME, needs your admin password — run yourself):
#   brew install --cask xquartz
#   # then log out + back in (first install only), then:
#   defaults write org.xquartz.X11 nolisten_tcp -bool false
#   defaults write org.xquartz.X11 enable_iglx -bool true
#   open -a XQuartz            # start the X server
#   /opt/X11/bin/xhost +localhost
#
# Then just run:  bash emulator/guppy_xquartz_mac.sh   [HwSetup|HwViewer|SParDialog]
set -u
VM=tnc
GUPPY_C="${1:-HwSetup}"
SSHCFG="$HOME/.lima/$VM/ssh.config"
HOSTALIAS="lima-$VM"

command -v xquartz >/dev/null 2>&1 || ls /opt/X11/bin/Xquartz >/dev/null 2>&1 || {
  echo "XQuartz not installed. Run: brew install --cask xquartz (then log out/in once)"; exit 1; }
[ -f "$SSHCFG" ] || { echo "lima ssh config not found: $SSHCFG (is the VM running? limactl start $VM)"; exit 1; }

echo "=== ensure XQuartz is running + accepts local TCP ==="
open -a XQuartz 2>/dev/null; sleep 2
/opt/X11/bin/xhost +localhost >/dev/null 2>&1 || true
# verify XQuartz is listening on 6000 (nolisten_tcp must be false; needs a logout after the defaults write)
if ! (lsof -nP -iTCP:6000 -sTCP:LISTEN >/dev/null 2>&1); then
  echo "!! XQuartz is NOT listening on TCP 6000."
  echo "   Run: defaults write org.xquartz.X11 nolisten_tcp -bool false ; then LOG OUT + back in, reopen XQuartz."
  exit 1
fi

echo "=== open reverse SSH tunnel: VM localhost:6000 -> Mac XQuartz :0 ==="
# kill any stale tunnel, then start a fresh persistent one over lima's ssh config
pkill -f "ssh.*-NR.*6000:localhost:6000.*$HOSTALIAS" 2>/dev/null; sleep 1
ssh -F "$SSHCFG" -o ExitOnForwardFailure=yes -fNR 6000:localhost:6000 "$HOSTALIAS" \
  && echo "  reverse tunnel up" || { echo "  tunnel failed"; exit 1; }

echo "=== launch Guppy ($GUPPY_C) FEX-native, rendering to the Mac XQuartz (DISPLAY=localhost:0) ==="
echo "    A native macOS window titled 'HwViewer' should appear shortly."
limactl shell "$VM" -- env XDISPLAY=localhost:0 GUPPY_C="$GUPPY_C" MMI_TIMEOUT="${MMI_TIMEOUT:-300}" \
  bash emulator/run_guppy_window.sh

echo "=== done. closing reverse tunnel ==="
pkill -f "ssh.*-NR.*6000:localhost:6000.*$HOSTALIAS" 2>/dev/null
