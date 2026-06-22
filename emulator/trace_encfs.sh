#!/bin/bash
# trace_encfs.sh — capture ConfigServer's encDir encfs invocation (binary + args + the -S stdin password)
# so we can pre-populate _jh_int under the SAME password. Puts encfs+fusermount on PATH + /dev/fuse so the
# mount can actually succeed. Run in VM tnc:  limactl shell tnc -- bash <repo>/emulator/trace_encfs.sh
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$REPO/work/control/sysroot"
SRC="$REPO/work/target/rootfs"
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
PRE="/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== rebuild preloads + stage encfs/fusermount into rootfs ==="
$CC -shared -fPIC -O2 -Wl,--version-script="$REPO/emulator/arena.map" -o "$R/lib/arena_stub.so" "$REPO/emulator/arena_stub.c" || exit 1
for s in herosapi_shim heros_rtos renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" || exit 1; done
# stage the control's i386 encfs + fusermount + their closure (deref symlinks)
sudo bash -c '
SRC="'"$SRC"'"; R="'"$R"'"
for b in usr/bin/encfs usr/bin/fusermount bin/fusermount sbin/fusermount; do
  [ -e "$SRC/$b" ] && { mkdir -p "$R/$(dirname $b)"; cp -aL "$SRC/$b" "$R/$b" 2>/dev/null; }
done
fl(){ find "$SRC/heros5/bin" "$SRC/usr/lib" "$SRC/lib" -name "$1" 2>/dev/null|head -1; }
for l in libfuse.so.2 librlog.so.5 libssl.so.1.1 libcrypto.so.1.1 libssl.so libcrypto.so; do
  p=$(fl "$l"); [ -n "$p" ] && { rel=${p#$SRC/}; mkdir -p "$R/$(dirname $rel)"; cp -aL "$p" "$R/$rel" 2>/dev/null; }
done
'
echo "  encfs in rootfs: $([ -e $R/usr/bin/encfs ] && echo yes || echo NO); fusermount: $([ -e $R/usr/bin/fusermount ] && echo yes || echo NO)"

ln -sfn "$CFG" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* 2>/dev/null
sudo mkdir -p /mnt/sys/cache/nckern/productid
printf "16\n" | sudo tee /mnt/sys/cache/nckern/productid/controlmark.conf >/dev/null
for f in exportversion:1 ncstate:3 progstationversion:1 virtualmachine:0; do
  printf "%s\n" "${f#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${f%%:*}.conf >/dev/null; done
sudo chmod -R a+r /mnt/sys/cache
# reset the encfs store so encDir creates a clean one we can observe
sudo rm -rf /mnt/sys/config/_jh_int /mnt/sys/config/jh_int
sudo mkdir -p /mnt/sys/config/_jh_int /mnt/sys/config/jh_int; sudo chmod 777 /mnt/sys/config/_jh_int /mnt/sys/config/jh_int

GUARD_BEFORE=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/encfs_trace.log /tmp/encfs_cfg.log

sudo env R="$R" PRE="$PRE" \
  SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: \
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 \
  HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 \
  LANG=C LC_ALL=C LD_PRELOAD="$PRE" \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /
    mount --bind "$R/etc" /etc
    [ -e /dev/fuse ] || { mknod /dev/fuse c 10 229 2>/dev/null; chmod 666 /dev/fuse; }
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
    export PATH="$R/usr/bin:$R/usr/sbin:$R/bin:$R/sbin:$PATH"   # so encDir finds encfs+fusermount
    cd /
    # -f follow forks; capture execve (the encfs command) + write (the -S stdin password) + the pipe
    timeout -s KILL 50 /usr/bin/strace -f -qq -s 200 -e trace=execve,write,read -o /tmp/encfs_trace.log \
      FEXInterpreter "$R/heros5/bin/ConfigServer.elf" -p=~/cfgserver cfgserver \
        -f=/tmp/s/config/jhconfigfiles.cfg -i=Nc > /tmp/encfs_cfg.log 2>&1
    pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null
  '
GUARD_AFTER=$(md5sum /etc/passwd | awk '{print $1}')
[ "$GUARD_BEFORE" = "$GUARD_AFTER" ] && echo "GUARD OK /etc unchanged" || echo "*** GUARD /etc CHANGED ***"

echo "=== encfs / fusermount execve invocations (binary + args) ==="
grep -aE "execve\(.*(encfs|fusermount)" /tmp/encfs_trace.log | sed -E 's/ = .*//' | head
echo "=== encDir stdout (mount success/fail) ==="
grep -aiE "encdir|encfs|mount|fuse" /tmp/encfs_cfg.log | grep -avE "^\[t[0-9a-f]" | head -20
echo "=== writes to encfs (the -S password is a short write right after the encfs execve) ==="
grep -aE 'write\([0-9]+, ".{4,40}\\n"' /tmp/encfs_trace.log | grep -aviE "rtos|cfg|config|\[t" | head -20
echo "=== any 16-24 char alnum token written (candidate password) ==="
grep -aoE 'write\([0-9]+, "[A-Za-z0-9+/]{12,30}' /tmp/encfs_trace.log | head
echo "logs: /tmp/encfs_trace.log /tmp/encfs_cfg.log"
