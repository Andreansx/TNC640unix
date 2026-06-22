#!/bin/bash
# run_3servers_fex.sh — bring up the THREE boot-chain system servers TOGETHER under FEX, in ONE
# contained mount namespace: dbus (S20) + heros-auth-daemon (S23) + heuserver (S77). Each has been
# proven individually; this validates they COEXIST (the documented prerequisite for AppStartMP, which
# forks constellation children that connect to all three). All /etc writes contained to the rootfs;
# real guest /etc guarded by md5 (heuserver-as-root corruption protection).
#
# Run inside lima `tnc` as the normal user (sudo used for the namespace + root servers).
set -u
R=/var/tmp/lr
EMU="$(cd "$(dirname "$0")" && pwd)"
SRC=/Users/andreansx/Documents/TNC640unix/work/target/rootfs
CC=i686-linux-gnu-gcc
FEXLIBS=/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu

echo "=== [0] sanity + closures ==="
which FEXInterpreter >/dev/null || { echo "FATAL: no FEXInterpreter"; exit 1; }
for b in usr/sbin/heuserver usr/bin/dbus-daemon usr/sbin/heros-auth-daemon usr/bin/fusermount; do
  [ -e "$R/$b" ] || echo "  NOTE: $R/$b missing (run the per-server scripts once to copy closures)"
done

echo "=== [1] rebuild i386 preloads ==="
for s in herosapi_shim renamefix fexunmask heros_rtos fakeroot; do
  [ -f "$EMU/$s.c" ] && $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$EMU/$s.c" 2>&1 | sed "s/^/  $s: /"
done

echo "=== [2] FEX config + clean stale state ==="
sudo mkdir -p /root/.fex-emu; printf '{"Config":{"RootFS":"%s"}}\n' "$R" | sudo tee /root/.fex-emu/Config.json >/dev/null
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
sudo rm -f /dev/shm/_heusrv_shm /dev/shm/hrctlU501 /dev/shm/hregU501_* 2>/dev/null

echo "=== [3] guard: real /etc baseline ==="
B_PASSWD=$(sudo md5sum /etc/passwd | cut -d' ' -f1); echo "  /etc/passwd md5 = $B_PASSWD"

LOG=/tmp/3srv.log; sudo rm -f "$LOG"
# One namespace, three backgrounded servers, each with its OWN preload set:
#   dbus, auth-daemon: herosapi_shim:renamefix
#   heuserver:        herosapi_shim:renamefix:fexunmask  (NO heros_rtos — it segfaults heuserver)
read -r -d '' NSCMD <<EOF
set -u
ulimit -c 0
mount --make-rprivate /
mount --bind $R/etc /etc
mount -t tmpfs tmpfs /run 2>/dev/null
mkdir -p /run/dbus /var/run/dbus /var/lib/dbus /etc/dbus-1
mkdir -p /var/run/auth_daemon/fs_mount /var/run/auth_daemon/certs /tmp/auth_daemon /mnt/auth_daemon
chmod 777 /tmp/auth_daemon /mnt/auth_daemon 2>/dev/null
mkdir -p /etc/sysconfig/heuseradmin /etc/security /etc/sysconfig/heros-auth-daemon
[ -e /etc/netgroup ] || : > /etc/netgroup
[ -s /etc/machine-id ] || printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id
ln -sf /etc/machine-id /var/lib/dbus/machine-id
[ -e /dev/fuse ] || { mknod /dev/fuse c 10 229 2>/dev/null; chmod 666 /dev/fuse 2>/dev/null; }
export PATH=$R/usr/bin:$R/bin:$R/sbin:\$PATH
export LANG=C LC_ALL=C LD_LIBRARY_PATH=$FEXLIBS
cd /

echo '### S20 dbus-daemon --system ###'
LD_PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so \
  FEXInterpreter $R/usr/bin/dbus-daemon --system --nofork --nopidfile --nosyslog >/tmp/d_dbus.log 2>&1 &
sleep 4

echo '### S23 heros-auth-daemon ###'
cat > /etc/sysconfig/heros-auth-daemon/daemon.conf <<CFG
[daemon]
fuse_mountpoint = /var/run/auth_daemon/certs/
srv_socket = /var/run/auth_daemon/auth-daemon-srv.sock
log_rule = *.debug=false\n;

[misc_data_mount]
root_path = /var/run/auth_daemon/fs_mount/
CFG
LD_PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so \
  FEXInterpreter $R/usr/sbin/heros-auth-daemon -d \
    -c /etc/sysconfig/heros-auth-daemon/daemon.conf \
    -p /var/run/auth_daemon/heros-auth-daemon.pid -l /tmp/d_authd.log >/tmp/d_authd_out.log 2>&1 &
sleep 5

echo '### S77 heuserver (no -d: blocks in accept loop = healthy) ###'
LD_PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so:/lib/fexunmask.so \
  FEXInterpreter $R/usr/sbin/heuserver >/tmp/d_heu.log 2>&1 &
sleep 6

echo '======== FOUNDATION STATUS ========'
echo '--- dbus system bus socket ---'
ls -l /run/dbus/system_bus_socket /var/run/dbus/system_bus_socket 2>/dev/null | head -1 || echo '  no dbus socket'
echo '--- auth-daemon srv socket + FUSE mounts ---'
ls -l /var/run/auth_daemon/auth-daemon-srv.sock 2>/dev/null || echo '  no auth-daemon socket'
mount | grep -iE 'auth_daemon|fuse' | grep -v fusectl | head -3
echo '--- heuserver TCP 127.0.0.1:19093 ---'
(ss -ltnp 2>/dev/null || netstat -ltnp 2>/dev/null) | grep ':19093' || echo '  heuserver NOT listening'
echo '--- live FEX server processes ---'
pgrep -af FEXInterpreter | grep -oE '(dbus-daemon|heros-auth-daemon|heuserver)' | sort | uniq -c
echo '===================================='
sleep 1
pkill -KILL -x FEXInterpreter 2>/dev/null
EOF

echo "=== [4] run the 3-server foundation (contained) ==="
sudo unshare -m bash -c "$NSCMD" >"$LOG" 2>&1
grep -vE "cannot be preloaded|object .* from LD_PRELOAD" "$LOG"

echo "=== [5] guard: real /etc unchanged? ==="
A_PASSWD=$(sudo md5sum /etc/passwd | cut -d' ' -f1)
[ "$A_PASSWD" = "$B_PASSWD" ] && echo "  SAFE: /etc/passwd md5 unchanged" || echo "  *** WARNING: /etc/passwd CHANGED ($B_PASSWD -> $A_PASSWD) ***"
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
echo "logs: /tmp/3srv.log /tmp/d_dbus.log /tmp/d_authd.log /tmp/d_heu.log"