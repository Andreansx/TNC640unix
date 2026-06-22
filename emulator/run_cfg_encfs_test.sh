#!/bin/bash
# run_cfg_encfs_test.sh — decisive config-#6 attempt: populate the encfs config store _jh_int (under
# encDir's CONFIRMED password Yomxn8YJyvrbNli62Rpl) with the real plaintext config, then run
# ConfigServer+IPO with a SUCCEEDING encfs + controlmark=16, and check whether ConfigServer finally
# OPENS the data files / registers layers / IPO -k=NC passes.
#
# IMPORTANT: does NOT bind /etc and does NOT set LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu — that
# combo leaks the 64-bit host libfuse into the i386 guest encfs (ELFCLASS64). run_fuse_test.sh proves
# encfs works under FEX precisely without that. We protect /etc with an md5 guard instead.
# Usage: limactl shell tnc -- bash <repo>/emulator/run_cfg_encfs_test.sh
set -u
PW="Yomxn8YJyvrbNli62Rpl"   # encDir's encfs password (captured at runtime, trace_encfs.sh)
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$REPO/work/control/sysroot"
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
PRE="/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== rebuild preloads ==="
$CC -shared -fPIC -O2 -Wl,--version-script="$REPO/emulator/arena.map" -o "$R/lib/arena_stub.so" "$REPO/emulator/arena_stub.c" || exit 1
for s in herosapi_shim heros_rtos renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" || exit 1; done

ln -sfn "$CFG" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* 2>/dev/null
sudo mkdir -p /mnt/sys/cache/nckern/productid
printf "16\n" | sudo tee /mnt/sys/cache/nckern/productid/controlmark.conf >/dev/null
for kv in exportversion:1 ncstate:3 progstationversion:1 virtualmachine:0; do
  printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%%:*}.conf >/dev/null; done
sudo chmod -R a+r /mnt/sys/cache
[ -e /dev/fuse ] || { sudo mknod /dev/fuse c 10 229; sudo chmod 666 /dev/fuse; }

GUARD_BEFORE=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/cfgenc_pop.log /tmp/cfgenc_cfg.log /tmp/cfgenc_ipo.log /tmp/cfgenc_strace.log

echo "=== [A] (re)create _jh_int as a FRESH encfs under the confirmed PW, then populate ==="
sudo fusermount -u /mnt/sys/config/jh_int 2>/dev/null
# Reset: a fresh store created with --standard -S so the volume key is encrypted with EXACTLY PW
# (encDir later reads this .encfs6.xml O_RDONLY and mounts with PW). Without --standard the password
# handling is ambiguous (first stdin line consumed as the config-mode answer -> wrong key).
sudo rm -rf /mnt/sys/config/_jh_int /mnt/sys/config/jh_int
sudo mkdir -p /mnt/sys/config/_jh_int /mnt/sys/config/jh_int
sudo chmod 777 /mnt/sys/config/_jh_int /mnt/sys/config/jh_int
sudo PATH="$R/usr/bin:$R/usr/sbin:$PATH" bash -c '
  R=/var/tmp/lr; CFG="'"$CFG"'"; PW="'"$PW"'"
  printf "%s\n" "$PW" | FEXInterpreter "$R/usr/bin/encfs" --standard -S /mnt/sys/config/_jh_int /mnt/sys/config/jh_int >/tmp/cfgenc_pop.log 2>&1 &
  sleep 6
  if mount | grep -q "/mnt/sys/config/jh_int"; then echo "  ENCFS MOUNTED (fresh, PW=encDir password)"; else echo "  ENCFS MOUNT FAILED:"; tail -6 /tmp/cfgenc_pop.log; fi
  cp -aL "$CFG"/config/*.cfg "$CFG"/config/*.atr /mnt/sys/config/jh_int/ 2>/dev/null
  cp -aL "$CFG"/config/layout /mnt/sys/config/jh_int/layout 2>/dev/null
  sync; sleep 1
  echo "  files visible in decrypted jh_int: $(ls /mnt/sys/config/jh_int/*.cfg /mnt/sys/config/jh_int/*.atr 2>/dev/null | wc -l)"
  fusermount -u /mnt/sys/config/jh_int 2>/dev/null; sleep 1
  pkill -KILL -x FEXInterpreter 2>/dev/null
'
echo "  _jh_int encrypted entries now: $(sudo bash -c 'ls /mnt/sys/config/_jh_int 2>/dev/null | grep -vc encfs6')"

# ConfigServer's run-up forks a heuseradmin path that opens /dev/shm/_heusrv_shm — create it or it segfaults.
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0644 /dev/shm/_heusrv_shm'

echo "=== [B] run ConfigServer (bg) + IPO (fg), controlmark=16, encfs available ==="
sudo PATH="$R/usr/bin:$R/usr/sbin:/usr/bin:/bin" \
  SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: \
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 \
  HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 \
  LANG=C LC_ALL=C LD_PRELOAD="$PRE" \
  bash -c '
    R=/var/tmp/lr; cd /
    ( timeout -s KILL 120 strace -f -qq -e trace=openat -o /tmp/cfgenc_strace.log \
        FEXInterpreter "$R/heros5/bin/ConfigServer.elf" -p=~/cfgserver cfgserver \
          -f=/tmp/s/config/jhconfigfiles.cfg -i=Nc > /tmp/cfgenc_cfg.log 2>&1 ) &
    i=0; while [ $i -lt 120 ]; do grep -q "HWS stub: replied" /tmp/cfgenc_cfg.log 2>/dev/null && break; sleep 0.5; i=$((i+1)); done
    sleep 5
    timeout -s KILL 70 FEXInterpreter "$R/heros5/bin/ipo_progstation.elf" -p=~/IPO IPO -k=NC -M > /tmp/cfgenc_ipo.log 2>&1 || true
    pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null
  '
GUARD_AFTER=$(md5sum /etc/passwd | awk '{print $1}')
[ "$GUARD_BEFORE" = "$GUARD_AFTER" ] && echo "GUARD OK /etc unchanged" || echo "*** GUARD /etc CHANGED ($GUARD_BEFORE -> $GUARD_AFTER) ***"

echo "=== RESULTS ==="
echo "--- encDir encfs mount in ConfigServer (success vs error encfs) ---"
grep -aiE "encdir: (mounted|started|error)|error encfs" /tmp/cfgenc_cfg.log | head
echo "--- ★ ConfigServer opens jh_int CONTENTS / data files? (encrypted blob reads = succeeding encfs) ---"
echo "    jh_int sub-opens: $(grep -acE 'jh_int/[^ ]' /tmp/cfgenc_strace.log)"
grep -aE 'jh_int/' /tmp/cfgenc_strace.log | grep -a openat | sed -E 's/.*openat\(//' | grep -avE '\.encfs6|jh_int"|jh_int,' | head -8
echo "    tnc.cfg/ChannelCfg opens: $(grep -acE 'tnc\.cfg|ChannelCfg|GlobalSystemCfg' /tmp/cfgenc_strace.log)"
echo "--- ★★ IPO -k=NC outcome ---"
grep -avE '^\[rtos\]|^\[t[0-9]|cannot be preloaded|from LD_PRELOAD' /tmp/cfgenc_ipo.log | grep -iE 'connected|invalid command|checkoptions|channel|condition' | head
echo "logs: /tmp/cfgenc_cfg.log /tmp/cfgenc_ipo.log /tmp/cfgenc_strace.log /tmp/cfgenc_pop.log"
