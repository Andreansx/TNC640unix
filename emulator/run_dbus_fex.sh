#!/bin/bash
# run_dbus_fex.sh — bring up dbus-daemon --system (the foundational system bus, S20) under
# FEX, contained (mount-ns /etc + writable /var/run, /var/lib). RTOS-free, no license.
# Scouts the first blocker / confirms it binds /var/run/dbus/system_bus_socket. Run in VM tnc.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"; SRC="$REPO/work/target/rootfs"; R=/var/tmp/lr
CC=i686-linux-gnu-gcc; PRE=/lib/herosapi_shim.so:/lib/renamefix.so

echo "=== [1] copy dbus-daemon + closure + config into $R ==="
copylib(){ local lib="$1" p rel; p=$(find "$SRC/usr/lib" "$SRC/lib" -name "$lib" 2>/dev/null|head -1); [ -z "$p" ]&&return
  rel=${p#$SRC/}; case "$lib" in libc.so.6|libpthread.so.*|librt.so.*|libdl.so.*|libm.so.6|ld-linux*)return;;esac
  [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"&&return
  sudo mkdir -p "$R/$(dirname "$rel")"; sudo rm -f "$R/$rel"; sudo cp -aL "$p" "$R/$rel"; echo "  + $rel"; resolve "$rel"; }
resolve(){ for lib in $(i686-linux-gnu-objdump -p "$SRC/$1" 2>/dev/null|awk '/NEEDED/{print $2}'); do copylib "$lib"; done; }
sudo cp -aL "$SRC/usr/bin/dbus-daemon" "$R/usr/bin/dbus-daemon"; resolve usr/bin/dbus-daemon
# config trees
sudo mkdir -p "$R/usr/share/dbus-1" "$R/etc/dbus-1"
sudo cp -aL "$SRC/usr/share/dbus-1/system.conf" "$R/usr/share/dbus-1/" 2>/dev/null
[ -d "$SRC/usr/share/dbus-1/system.d" ] && sudo cp -aL "$SRC/usr/share/dbus-1/system.d" "$R/usr/share/dbus-1/" 2>/dev/null
[ -d "$SRC/etc/dbus-1" ] && sudo cp -aL "$SRC/etc/dbus-1/." "$R/etc/dbus-1/" 2>/dev/null
for s in herosapi_shim renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" 2>/dev/null; done

echo "=== [2] run dbus-daemon --system contained — capture first blocker ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /; mount --bind $R/etc /etc
  # ISOLATE /run so the FEX dbus binds in a PRIVATE tmpfs and never touches the VM's own
  # /run/dbus/system_bus_socket (protect the VM — see the /etc-leak lesson).
  mount -t tmpfs tmpfs /run; mkdir -p /run/dbus
  mkdir -p /var/run/dbus /var/lib/dbus /etc/dbus-1
  # machine-id (32 hex) — dbus requires it
  [ -s /etc/machine-id ] || printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id
  ln -sf /etc/machine-id /var/lib/dbus/machine-id
  cd /
  timeout -s KILL 10 env LANG=C LC_ALL=C LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
    LD_PRELOAD=$PRE FEXInterpreter $R/usr/bin/dbus-daemon --system --nofork --nopidfile --nosyslog &
  DP=\$!
  sleep 5
  echo '--- system bus socket bound? ---'
  ls -l /var/run/dbus/system_bus_socket 2>/dev/null && echo 'SOCKET BOUND (dbus up)' || echo '  no socket'
  ss -lxp 2>/dev/null | grep -i dbus || true
  kill -9 \$DP 2>/dev/null
" >/tmp/dbus.log 2>&1
grep -vE "cannot be preloaded" /tmp/dbus.log | head -25
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; echo done
