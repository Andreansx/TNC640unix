#!/bin/bash
# run_fuse_test.sh — test whether FUSE works under FEX on ARM64, using the control's own i386
# encfs (the config-store mechanism). If encfs mounts + round-trips a file, FUSE-under-FEX works,
# which unblocks heros-auth-daemon AND the ConfigServer encfs config store. Run in VM tnc (root).
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"; SRC="$REPO/work/target/rootfs"; R=/var/tmp/lr
CC=i686-linux-gnu-gcc; PRE=/lib/herosapi_shim.so:/lib/renamefix.so

echo "=== [1] copy encfs + fusermount + closures into $R ==="
copylib(){ local lib="$1" p rel; p=$(find "$SRC/usr/lib" "$SRC/lib" -name "$lib" 2>/dev/null|head -1); [ -z "$p" ]&&return
  rel=${p#$SRC/}; case "$lib" in libc.so.6|libpthread.so.*|librt.so.*|libdl.so.*|libm.so.6|ld-linux*)return;;esac
  [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"&&return
  sudo mkdir -p "$R/$(dirname "$rel")"; sudo rm -f "$R/$rel"; sudo cp -aL "$p" "$R/$rel"; echo "  + $rel"; resolve "$rel"; }
resolve(){ for lib in $(i686-linux-gnu-objdump -p "$SRC/$1" 2>/dev/null|awk '/NEEDED/{print $2}'); do copylib "$lib"; done; }
for bin in usr/bin/encfs usr/bin/fusermount; do sudo cp -aL "$SRC/$bin" "$R/$bin"; resolve "$bin"; done
for s in herosapi_shim renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" 2>/dev/null; done

echo "=== [2] mount an encfs filesystem under FEX (contained) ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /
  mount -t tmpfs tmpfs /tmp; mkdir -p /tmp/_enc /tmp/dec   # source (encrypted) + mountpoint
  export PATH=$R/usr/bin:$R/bin:\$PATH                      # so encfs finds fusermount
  cd /tmp
  # encfs --standard (non-interactive preset) -S (password from stdin) -f (foreground) -v
  printf 'testpassword123\n' | timeout -s KILL 12 env LANG=C LC_ALL=C \
    LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu LD_PRELOAD=$PRE \
    FEXInterpreter $R/usr/bin/encfs --standard -S -f /tmp/_enc /tmp/dec >/tmp/encfs.log 2>&1 &
  EP=\$!
  sleep 5
  echo '--- is /tmp/dec a FUSE mount? ---'
  mount | grep -E '/tmp/dec' && echo 'ENCFS MOUNTED (FUSE works under FEX!)' || echo '  NOT mounted'
  echo '--- round-trip a file through the encfs ---'
  echo 'hello-fuse-fex' > /tmp/dec/secret.txt 2>/tmp/wr.err && echo '  wrote /tmp/dec/secret.txt' || { echo '  write FAILED:'; cat /tmp/wr.err; }
  echo '  encrypted view in _enc:'; ls -la /tmp/_enc 2>/dev/null | grep -v '\.encfs6' | tail -2
  echo '  read back through dec:'; cat /tmp/dec/secret.txt 2>/dev/null || echo '   read failed'
  echo '--- unmount ---'; fusermount -u /tmp/dec 2>/dev/null || umount /tmp/dec 2>/dev/null
  kill -9 \$EP 2>/dev/null
  echo '--- encfs log (errors) ---'; grep -iE 'error|fail|fuse|mount|denied|cannot|permission' /tmp/encfs.log 2>/dev/null | head -8
" 2>&1 | grep -vE "cannot be preloaded"
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; echo done
