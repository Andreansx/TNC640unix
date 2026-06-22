#!/bin/bash
# run_hessrv_fex.sh — bring up hessrv (HeROS identity/license RPC server, S40) under FEX,
# contained (mount-ns /etc + writable /var/run). RTOS-free, so same preloads as heuserver.
# Scouts the first concrete blocker. Run in VM tnc.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"; SRC="$REPO/work/target/rootfs"; R=/var/tmp/lr
CC=i686-linux-gnu-gcc; PRE=/lib/herosapi_shim.so:/lib/renamefix.so

echo "=== [1] copy hessrv + its closure into $R (cp -aL deref symlinks; skip glibc) ==="
copylib(){ local lib="$1" p rel; p=$(find "$SRC/usr/lib" "$SRC/lib" -name "$lib" 2>/dev/null|head -1); [ -z "$p" ]&&return
  rel=${p#$SRC/}; case "$lib" in libc.so.6|libpthread.so.*|librt.so.*|libdl.so.*|libm.so.6|ld-linux*)return;;esac
  [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"&&return
  sudo mkdir -p "$R/$(dirname "$rel")"; sudo rm -f "$R/$rel"; sudo cp -aL "$p" "$R/$rel"; echo "  + $rel"; resolve "$rel"; }
resolve(){ for lib in $(i686-linux-gnu-objdump -p "$SRC/$1" 2>/dev/null|awk '/NEEDED/{print $2}'); do copylib "$lib"; done; }
sudo cp -aL "$SRC/usr/sbin/hessrv" "$R/usr/sbin/hessrv"; resolve usr/sbin/hessrv
for s in herosapi_shim renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" 2>/dev/null; done

echo "=== [2] run hessrv contained (writable /var/run, /etc bound) — capture first blocker ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /; mount --bind $R/etc /etc
  mkdir -p /var/run/hessrv /etc/sysconfig; cd /
  timeout -s KILL 12 env LANG=C LC_ALL=C LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
    LD_PRELOAD=$PRE FEXInterpreter $R/usr/sbin/hessrv
  echo HESSRV_EXIT=\$?
" >/tmp/hessrv.log 2>&1
echo "--- hessrv output (first blocker) ---"
grep -vE "cannot be preloaded" /tmp/hessrv.log | head -25
echo "--- listening unix socket? ---"
sudo ss -lxp 2>/dev/null | grep -i hessrv || echo "  no hessrv.sock"
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; echo done
