#!/bin/bash
# heu_client_test.sh — drive a REAL libheuseradmin client (heu_client.c) against the live
# heuserver and observe the end-to-end handshake (client connects → heuserver identifies the
# peer → grants/denies a ticket). Both run contained under FEX.
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/work/target/rootfs"          # source rootfs (host mount) with libheuseradmin + closure
R=/var/tmp/lr
UNMASK="${UNMASK:-1}"     # 1 = preload fexunmask.so into heuserver (unmask FEX client identity)
PRE=/lib/herosapi_shim.so:/lib/renamefix.so
[ "$UNMASK" = 1 ] && PRE=$PRE:/lib/fexunmask.so
CC=i686-linux-gnu-gcc

echo "=== [1] ensure libheuseradmin + its closure are in the FEX rootfs $R ==="
need=(usr/lib/libheuseradmin.so.1.17.4.929)
# recursively resolve NEEDED from the source rootfs and copy any missing into $R
# copy a soname into $R, DEREFERENCING symlinks (cp -aL) so the real i386 file lands at
# the soname path (a copied dangling symlink made the i386 loader fall back to a 64-bit lib).
copylib() { # $1 = soname (e.g. libcap.so.2)
  local lib="$1" p rel
  p=$(find "$SRC/usr/lib" "$SRC/lib" -name "$lib" 2>/dev/null | head -1); [ -z "$p" ] && return 1
  rel=${p#$SRC/}
  # skip glibc (modern glibc already in $R; old glibc would break FEX)
  case "$lib" in libc.so.6|libpthread.so.*|librt.so.*|libdl.so.*|libm.so.6|ld-linux*) return 0;; esac
  [ -e "$R/$rel" ] && file -L "$R/$rel" 2>/dev/null | grep -q "ELF 32-bit" && return 0
  sudo mkdir -p "$R/$(dirname "$rel")"; sudo rm -f "$R/$rel"; sudo cp -aL "$p" "$R/$rel"
  echo "  + $rel (i386)"
  resolve "$rel"
}
resolve() {
  local f="$1" lib
  for lib in $(i686-linux-gnu-objdump -p "$SRC/$f" 2>/dev/null | awk '/NEEDED/{print $2}'); do
    copylib "$lib"
  done
}
sudo mkdir -p "$R/usr/lib"
sudo rm -f "$R/lib/libcap.so.2" "$R/usr/lib/libpcre.so.1" "$R/usr/lib/libglib-2.0.so.0"  # purge prior bad copies
for f in "${need[@]}"; do
  sudo cp -aL "$SRC/$f" "$R/$f"; echo "  + $f"
  resolve "$f"
done
# soname symlink
sudo ln -sf libheuseradmin.so.1.17.4.929 "$R/usr/lib/libheuseradmin.so.1" 2>/dev/null
sudo ln -sf libheuseradmin.so.1.17.4.929 "$R/usr/lib/libheuseradmin.so" 2>/dev/null

echo "=== [1b] build fexunmask.so (unmask FEX client identity for heuserver) ==="
$CC -shared -fPIC -O2 -o /tmp/fexunmask.so "$REPO/emulator/fexunmask.c" 2>&1 | head -4
[ -f /tmp/fexunmask.so ] && sudo cp /tmp/fexunmask.so "$R/lib/fexunmask.so" && echo "  built fexunmask.so (UNMASK=$UNMASK)"

echo "=== [2] build the client (i386, dlopen at runtime — no old-glibc linking) ==="
rm -f /tmp/heu_client
$CC -O2 "$REPO/emulator/heu_client.c" -o /tmp/heu_client -ldl 2>&1 | head -8
# CLIENT_NAME: stage the client under this name. "testheuseradmin" matches heuserver's
# privilege pattern */testheuseradmin -> a GRANT (demo of positive auth via fexunmask);
# anything else -> denied (not a recognized heros binary).
CLIENT_NAME="${CLIENT_NAME:-testheuseradmin}"
# CLIENT_REL: rootfs-relative path to stage+run the client at. Default tmp/$CLIENT_NAME.
# Set e.g. mnt/sys/heros5/bin/ipotest.elf to match the hardcoded pattern /mnt/sys/heros5/bin*/*.elf
# (tests whether REAL constellation binaries get authorized without the -t test hook).
CLIENT_REL="${CLIENT_REL:-tmp/$CLIENT_NAME}"
[ -f /tmp/heu_client ] && { sudo mkdir -p "$R/$(dirname "$CLIENT_REL")"; sudo cp /tmp/heu_client "$R/$CLIENT_REL"; echo "  built + staged at /$CLIENT_REL: $(file /tmp/heu_client | cut -d, -f1-2)"; } || { echo "  BUILD FAILED"; exit 1; }

echo "=== [3] start heuserver (foreground, contained, bg) ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/_heusrv_shm /tmp/heuserve.log
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /; mount --bind $R/etc /etc
  mkdir -p /etc/sysconfig/heuseradmin /etc/security; : > /etc/netgroup; cd /
  env LANG=C LC_ALL=C HEU_UNMASK_DBG=${HEU_UNMASK_DBG:-1} FEXUNMASK_ROOTFS=$R LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu LD_PRELOAD=$PRE \
    FEXInterpreter $R/usr/sbin/heuserver ${HEU_T:+-t $HEU_T}
" >/tmp/heuserve.log 2>&1 &
HSPID=$!
sleep 6
sudo ss -ltnp 2>/dev/null | grep -q 19093 && echo "  heuserver listening 19093" || { echo "  heuserver FAILED to bind"; grep -vE 'cannot be preloaded' /tmp/heuserve.log|tail; sudo kill -9 $HSPID; exit 1; }

echo "=== [4] run the REAL client under FEX (contained) ==="
sudo rm -f /tmp/heuclient.log
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /; mount --bind $R/etc /etc; cd /
  timeout -s KILL 15 env LANG=C LC_ALL=C LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
    FEX_RootFS=$R FEXInterpreter $R/$CLIENT_REL
  echo CLIENT_EXIT=\$?
" >/tmp/heuclient.log 2>&1
echo "--- client output ---"; grep -vE "cannot be preloaded" /tmp/heuclient.log | tail -12

echo "=== [5] heuserver's view of the connection (real pid? grant/deny?) ==="
grep -vE "cannot be preloaded" /tmp/heuserve.log | grep -iE "Connection|HEUTicket|denied|pid|priv|Calculate uid|extract peer" | tail -10

echo "=== [6] cleanup ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sudo kill -9 $HSPID 2>/dev/null; wait 2>/dev/null; echo done
