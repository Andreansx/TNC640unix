#!/bin/bash
# guppy_xquartz_mac.sh — surface the FEX-native Guppy GTK window as a NATIVE macOS window via
# XQuartz (rootless, NO VNC). Runs ON THE MAC. Validated chain (no XQuartz TCP-listener, no logout):
#
#   Guppy.elf (FEX i386->ARM64, lima VM)  --DISPLAY=localhost:0-->  VM localhost:6000
#     --reverse SSH tunnel-->  Mac localhost:6000  --socat-->  /tmp/.X11-unix/X0  -->  XQuartz
#
# XQuartz uses unix-socket :0 (works immediately after install, no logout); a socat TCP<->unix bridge
# avoids needing XQuartz's TCP listener (which would need `nolisten_tcp=false` + a logout).
#
# Prereqs (one-time): brew install --cask xquartz ; brew install socat
# Usage:  bash emulator/guppy_xquartz_mac.sh [HwSetup|HwViewer|SParDialog]
set -u
VM=tnc
GUPPY_C="${1:-HwSetup}"
SSHCFG="$HOME/.lima/$VM/ssh.config"
HOSTALIAS="lima-$VM"
XSOCK=/tmp/.X11-unix/X0
PORT=6000

command -v socat >/dev/null 2>&1 || { echo "socat missing: brew install socat"; exit 1; }
[ -d /Applications/Utilities/XQuartz.app ] || { echo "XQuartz missing: brew install --cask xquartz"; exit 1; }
[ -f "$SSHCFG" ] || { echo "lima ssh config not found: $SSHCFG (limactl start $VM)"; exit 1; }

echo "=== 1) start XQuartz + open its :0 unix socket ==="
open -a XQuartz 2>/dev/null
for i in $(seq 1 15); do [ -S "$XSOCK" ] && break; sleep 1; done
[ -S "$XSOCK" ] || { echo "XQuartz :0 socket ($XSOCK) did not appear"; exit 1; }
DISPLAY=:0 /opt/X11/bin/xhost + >/dev/null 2>&1   # localhost-only via the tunnel; disable cookie auth

echo "=== 2) socat bridge: Mac TCP 127.0.0.1:$PORT -> XQuartz $XSOCK ==="
pkill -f "TCP-LISTEN:$PORT" 2>/dev/null; sleep 1
socat TCP-LISTEN:$PORT,reuseaddr,fork,bind=127.0.0.1 UNIX-CONNECT:$XSOCK >/tmp/socat_x.log 2>&1 &
SOCAT_PID=$!; sleep 1
echo "  socat pid $SOCAT_PID"

echo "=== 3) reverse SSH tunnel: VM localhost:$PORT -> Mac localhost:$PORT ==="
pkill -f "NR $PORT:localhost:$PORT" 2>/dev/null; sleep 1
ssh -F "$SSHCFG" -o ExitOnForwardFailure=yes -fNR $PORT:localhost:$PORT "$HOSTALIAS" \
  && echo "  reverse tunnel up" || { echo "  tunnel failed"; kill $SOCAT_PID 2>/dev/null; exit 1; }

echo "=== 4) launch Guppy ($GUPPY_C) FEX-native -> Mac XQuartz (DISPLAY=localhost:0) ==="
echo "    A native macOS window titled 'HwViewer' should appear shortly (XQuartz)."
limactl shell "$VM" -- env XDISPLAY=127.0.0.1:0 GUPPY_C="$GUPPY_C" MMI_TIMEOUT="${MMI_TIMEOUT:-300}" \
  bash emulator/run_guppy_window.sh

echo "=== done; tearing down bridge + tunnel ==="
kill $SOCAT_PID 2>/dev/null
pkill -f "NR $PORT:localhost:$PORT" 2>/dev/null
