#!/bin/bash
# trace_cfgload.sh — run ConfigServer ALONE under FEX with the config-LOAD path triggered
# (INJECT_REREAD) and the productid control-mark set (default 16 = Tnc640 -> GetOptionTableTnc640),
# strace openat to see whether ConfigServer now OPENS tnc.cfg / ChannelCfg.atr (it NEVER did with
# controlmark=0 -> GetOptionTableNone -> empty layers). This is the decisive config-#6 signal.
# Run in VM tnc:  limactl shell tnc -- bash <repo>/emulator/trace_cfgload.sh [CONTROLMARK]
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$REPO/work/control/sysroot"
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
CM="${1:-16}"
PRE="/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== controlmark=$CM ; rebuild preloads ==="
$CC -shared -fPIC -O2 -Wl,--version-script="$REPO/emulator/arena.map" -o "$R/lib/arena_stub.so" "$REPO/emulator/arena_stub.c" || exit 1
for s in herosapi_shim heros_rtos renamefix; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" || exit 1; done

ln -sfn "$CFG" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* 2>/dev/null
# productid cache (control-mark drives OptionLib::GetOptionTable -> which builder -> which layers)
sudo mkdir -p /mnt/sys/cache/nckern/productid
printf "%s\n" "$CM" | sudo tee /mnt/sys/cache/nckern/productid/controlmark.conf >/dev/null
# REAL boot-generated values harvested from yeen's guest (controlmark=16, virtualmachine=1, ncstate=1)
printf "0\n" | sudo tee /mnt/sys/cache/nckern/productid/exportversion.conf >/dev/null
printf "1\n" | sudo tee /mnt/sys/cache/nckern/productid/ncstate.conf >/dev/null
printf "1\n" | sudo tee /mnt/sys/cache/nckern/productid/progstationversion.conf >/dev/null
printf "1\n" | sudo tee /mnt/sys/cache/nckern/productid/virtualmachine.conf >/dev/null
sudo chmod -R a+r /mnt/sys/cache

GUARD_BEFORE=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/cfgload.log /tmp/cfgload_strace.log

sudo env R="$R" PRE="$PRE" CM="$CM" \
  SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: \
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 \
  HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 \
  LANG=C LC_ALL=C LD_PRELOAD="$PRE" \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /
    mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
    cd /
    timeout -s KILL 60 /usr/bin/strace -f -qq -e trace=openat -o /tmp/cfgload_strace.log \
      FEXInterpreter "$R/heros5/bin/ConfigServer.elf" -p=~/cfgserver cfgserver \
        -f=/tmp/s/config/jhconfigfiles.cfg -i=Nc > /tmp/cfgload.log 2>&1
    pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null
  '
GUARD_AFTER=$(md5sum /etc/passwd | awk '{print $1}')
[ "$GUARD_BEFORE" = "$GUARD_AFTER" ] && echo "GUARD OK /etc unchanged" || echo "*** GUARD /etc CHANGED ***"

echo "=== productid confs opened by ConfigServer? ==="
grep -aE "productid/.*\.conf" /tmp/cfgload_strace.log | sed -E 's/.*openat\(//' | sort | uniq -c | head
echo "=== ★ data config files OPENED? (tnc.cfg / ChannelCfg / *.atr / layout) — the #6 signal ==="
grep -aE 'tnc\.cfg|Channel|\.atr|/config/.*\.cfg|jh_int|layout|GlobalSystem' /tmp/cfgload_strace.log | grep -a "openat" | sed -E 's/.*openat\(//' | sort -u | head -40
echo "    total data-cfg opens: $(grep -acE 'tnc\.cfg|Channel|\.atr|GlobalSystem|/config/[^ ]*\.cfg' /tmp/cfgload_strace.log)"
echo "=== ConfigServer config-load evidence in stdout (ReadConfigDataSet / QEvtServer broadcast) ==="
grep -aiE "ReadConfigData|QEvtServer|broadcast|config.*broadcast|541|4380|datafiles|CntDataFiles|RetrieveLayer|layer" /tmp/cfgload.log 2>/dev/null | grep -avE "^\[t[0-9a-f]" | head -15
echo "=== any option/sik/productid stdout ==="
grep -aiE "option|sik|productid|controlmark|GetOptionTable" /tmp/cfgload.log 2>/dev/null | grep -avE "^\[t[0-9a-f]|Q_create|Q_ident|Q_read" | head -10
echo "logs: /tmp/cfgload.log /tmp/cfgload_strace.log"
