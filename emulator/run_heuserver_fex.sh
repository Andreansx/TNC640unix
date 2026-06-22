#!/bin/bash
# run_heuserver_fex.sh — run heuserver (HeROS user/login server) under FEX on ARM64,
# SAFELY contained so its credential writes can never corrupt the real guest /etc.
#
# WHY containment: FEX RootFS does NOT redirect /etc *writes* to the rootfs — an
# absolute-path write to /etc/passwd from the FEX-emulated i386 process lands in the
# REAL guest /etc (proven with a static i386 probe). heuserver runs as root and
# rewrites /etc/passwd|group|shadow|security, so an unguarded run WIPES the lima
# user out of the guest /etc/passwd and breaks SSH (this happened; recovered via
# offline disk surgery). So we run heuserver inside a mount namespace with the rootfs
# /etc bind-mounted over /etc — writes are contained to /var/tmp/lr/etc, real /etc safe.
#
# heuserver is a real TCP server: after credential setup it binds 127.0.0.1:19093
# (decompiled sa_data 4a 95 7f 00 00 01) and runs a poll/accept loop. init.d runs it
# as `heuserver -d` (daemonize). WITHOUT -d it blocks in the accept loop in the
# foreground — that is the DECISIVE health test: healthy => stays alive + listening.
#
# Run inside lima `tnc` as the normal user (script uses sudo for the namespace+root).
set -u
R=/var/tmp/lr
EMU="$(cd "$(dirname "$0")" && pwd)"
CC=i686-linux-gnu-gcc
MODE="${1:-foreground}"                  # foreground (no -d) | daemon (-d)
# NOTE: heros_rtos.so is DELIBERATELY EXCLUDED — the RTOS emulator (needed by the
# i386 NCK/IPO which issue heroscall syscall(222)) SEGFAULTS heuserver. heuserver
# only needs the /dev/herosapi stub + the EXDEV rename fix. With heros_rtos in the
# preload list heuserver crashes (exit 139) right after "Updated /etc/security/groups";
# without it, heuserver reaches "Created stream socket" and binds 127.0.0.1:19093.
PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so
FEXLIBS=/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu

echo "=== [0] sanity ==="
which FEXInterpreter >/dev/null || { echo "FATAL: no FEXInterpreter"; exit 1; }
[ -x "$R/usr/sbin/heuserver" ] || { echo "FATAL: $R/usr/sbin/heuserver missing"; exit 1; }

echo "=== [1] rebuild i386 preloads into $R/lib ==="
for s in herosapi_shim heros_rtos renamefix fakeroot; do
  [ -f "$EMU/$s.c" ] && $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$EMU/$s.c" 2>&1 | sed "s/^/  $s: /"
done
echo "  preloads: $(ls "$R"/lib/{renamefix,herosapi_shim,heros_rtos}.so 2>/dev/null | wc -l)/3"

echo "=== [2] FEX config + clean stale shm ==="
sudo mkdir -p /root/.fex-emu; printf '{"Config":{"RootFS":"%s"}}\n' "$R" | sudo tee /root/.fex-emu/Config.json >/dev/null
sudo rm -f /dev/shm/_heusrv_shm /dev/shm/hrctlU501 /dev/shm/hregU501_* 2>/dev/null || true

echo "=== [3] guard: real /etc baseline ==="
B_PASSWD=$(sudo md5sum /etc/passwd | cut -d' ' -f1)
echo "  /etc/passwd md5 = $B_PASSWD"

ARGS=""; [ "$MODE" = daemon ] && ARGS="-d"
LOG=/tmp/heuserver_run.log; sudo rm -f "$LOG"

# The contained command: private mount-ns, bind rootfs/etc over /etc, pre-create the
# dirs heuserver self-gens into (within the contained rootfs etc), then exec heuserver.
read -r -d '' NSCMD <<EOF
mount --make-rprivate /
mount --bind $R/etc /etc
mkdir -p /etc/sysconfig/heuseradmin /etc/security
[ -e /etc/netgroup ] || : > /etc/netgroup
cd /
exec env LANG=C LC_ALL=C LD_LIBRARY_PATH=$FEXLIBS LD_PRELOAD=$PRELOAD \
  FEXInterpreter $R/usr/sbin/heuserver $ARGS
EOF

echo "=== [4] run heuserver $ARGS (contained, mode=$MODE) ==="
if [ "$MODE" = foreground ]; then
  sudo unshare -m bash -c "$NSCMD" >"$LOG" 2>&1 &
  NSPID=$!
  sleep 6
  echo "--- output ---"; sudo cat "$LOG" 2>/dev/null | tail -40
  echo "--- alive? ---"; if sudo kill -0 "$NSPID" 2>/dev/null; then echo "YES (blocking in accept loop — HEALTHY)"; else echo "NO (exited early)"; fi
  echo "--- listening 127.0.0.1:19093? ---"; (sudo ss -ltnp 2>/dev/null || sudo netstat -ltnp 2>/dev/null) | grep -E ":19093" || echo "  NOT listening"
  sudo pkill -TERM -P "$NSPID" 2>/dev/null; sudo kill "$NSPID" 2>/dev/null; wait "$NSPID" 2>/dev/null
else
  sudo unshare -m bash -c "$NSCMD" >"$LOG" 2>&1
  echo "--- parent exit: $? (init.d: 0/2 OK, 3 fail) ---"; sudo cat "$LOG" | tail -40
  sleep 2
  echo "--- listening 127.0.0.1:19093? ---"; (sudo ss -ltnp 2>/dev/null) | grep -E ":19093" || echo "  NOT listening"
fi

echo "=== [5] guard: real /etc unchanged? ==="
A_PASSWD=$(sudo md5sum /etc/passwd | cut -d' ' -f1)
[ "$A_PASSWD" = "$B_PASSWD" ] && echo "  SAFE: /etc/passwd md5 unchanged" || echo "  *** WARNING: /etc/passwd CHANGED ($B_PASSWD -> $A_PASSWD) ***"
echo "=== [6] heuserver's generated credential DB (in contained rootfs etc) ==="
ls -la "$R/etc/sysconfig/heuseradmin/" 2>/dev/null
ls -la "$R/etc/security/" 2>/dev/null
