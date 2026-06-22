#!/bin/bash
# run_authd_fex.sh — bring up heros-auth-daemon (S23, HeROS token/AD auth daemon) under FEX,
# contained. RTOS-free, no SIK. Scouts the first blocker. Run in VM tnc.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"; SRC="$REPO/work/target/rootfs"; R=/var/tmp/lr
CC=i686-linux-gnu-gcc; PRE=/lib/herosapi_shim.so:/lib/renamefix.so

echo "=== [1] copy heros-auth-daemon + closure into $R ==="
copylib(){ local lib="$1" p rel; p=$(find "$SRC/usr/lib" "$SRC/lib" -name "$lib" 2>/dev/null|head -1); [ -z "$p" ]&&return
  rel=${p#$SRC/}; case "$lib" in libc.so.6|libpthread.so.*|librt.so.*|libdl.so.*|libm.so.6|ld-linux*)return;;esac
  [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"&&return
  sudo mkdir -p "$R/$(dirname "$rel")"; sudo rm -f "$R/$rel"; sudo cp -aL "$p" "$R/$rel"; echo "  + $rel"; resolve "$rel"; }
resolve(){ for lib in $(i686-linux-gnu-objdump -p "$SRC/$1" 2>/dev/null|awk '/NEEDED/{print $2}'); do copylib "$lib"; done; }
sudo cp -aL "$SRC/usr/sbin/heros-auth-daemon" "$R/usr/sbin/heros-auth-daemon"; resolve usr/sbin/heros-auth-daemon
# fusermount — libfuse forks it to do the FUSE mount (FUSE works under FEX, see run_fuse_test.sh)
for bin in usr/bin/fusermount sbin/mount.fuse; do [ -e "$SRC/$bin" ] && { sudo cp -aL "$SRC/$bin" "$R/$bin"; resolve "$bin"; }; done
for s in herosapi_shim renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" 2>/dev/null; done

echo "=== [2] run heros-auth-daemon (foreground, contained) — capture first blocker ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /; mount --bind $R/etc /etc
  mount -t tmpfs tmpfs /run 2>/dev/null; mkdir -p /run /var/run
  mkdir -p /var/run/auth_daemon/fs_mount /var/run/auth_daemon/certs /tmp/auth_daemon /mnt/auth_daemon
  chmod 777 /tmp/auth_daemon /mnt/auth_daemon
  mkdir -p /etc/sysconfig/heros-auth-daemon /dev
  # real config (minus the hardware RFID/key-switch plugins) — provides the srv_socket the daemon binds
  cat > /etc/sysconfig/heros-auth-daemon/daemon.conf <<CFG
[daemon]
fuse_mountpoint = /var/run/auth_daemon/certs/
srv_socket = /var/run/auth_daemon/auth-daemon-srv.sock
log_rule = *.debug=false\n;

[misc_data_mount]
root_path = /var/run/auth_daemon/fs_mount/
CFG
  # provide /dev/fuse (bind the VM's real node) for the token filesystem
  [ -e /dev/fuse ] || { mknod /dev/fuse c 10 229 2>/dev/null; chmod 666 /dev/fuse 2>/dev/null; }
  export PATH=$R/usr/bin:$R/bin:$R/sbin:\$PATH    # so libfuse finds fusermount
  cd /
  # -d daemonize; -l logfile to capture the daemonized child's detailed stop reason
  timeout -s KILL 12 env LANG=C LC_ALL=C LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
    LD_PRELOAD=$PRE FEXInterpreter $R/usr/sbin/heros-auth-daemon -d \
      -c /etc/sysconfig/heros-auth-daemon/daemon.conf -p /var/run/auth_daemon/heros-auth-daemon.pid \
      -l /tmp/authd_detail.log &
  AP=\$!
  sleep 7
  echo '--- auth-daemon alive? ---'; pgrep -af FEXInterpreter | grep -c auth-daemon
  echo '--- FUSE mounts (cert store + token fs) ---'; mount | grep -iE 'auth_daemon|fuse' | grep -v fusectl
  echo '--- auth-daemon srv socket bound? ---'
  ls -l /var/run/auth_daemon/auth-daemon-srv.sock 2>/dev/null && echo 'SOCKET BOUND (auth-daemon up)' || echo '  no socket'
  ss -lxp 2>/dev/null | grep -iE 'auth.daemon' || true
  pkill -KILL -x FEXInterpreter 2>/dev/null; kill -9 \$AP 2>/dev/null
" >/tmp/authd.log 2>&1
grep -vE "cannot be preloaded" /tmp/authd.log | head -28
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; echo done
