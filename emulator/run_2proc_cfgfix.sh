set -u
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
CFGPRE="/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
IPOPRE="/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== build preloads (incl cfgfix) ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos renamefix; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c || exit 1; done

ln -sfn $CFG /tmp/s; ln -sfn $CFG/default/oem /tmp/o; ln -sfn $R/heros5/bin /tmp/b
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm 2>/dev/null
# productid cm=16 + OEM staging (the proven cfgresolve3 setup that makes the config load)
sudo mkdir -p /mnt/sys/cache/nckern/productid /mnt/plc/config
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do
  printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -a $CFG/default/oem/config/. /mnt/plc/config/ 2>/dev/null
sudo chmod -R a+r /mnt/sys/cache /mnt/plc/config
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'

GUARD=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/cfgsrv_cf.log /tmp/ipo_cf.log
sudo env R="$R" \
  SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: \
  CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 \
  HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 \
  LANG=C LC_ALL=C CFGPRE="$CFGPRE" IPOPRE="$IPOPRE" \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /; mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
    cd /
    echo "### ConfigServer (bg, with cfgfix) ###"
    ( LD_PRELOAD="$CFGPRE" timeout -s KILL 150 FEXInterpreter "$R/heros5/bin/ConfigServer.elf" \
        -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/cfgsrv_cf.log 2>&1 ) &
    CFGPID=$!
    i=0; while [ $i -lt 200 ]; do
      grep -q "HWS stub: replied" /tmp/cfgsrv_cf.log 2>/dev/null && { echo "  ConfigServer run-up complete at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    sleep 6
    echo "  ConfigServer: ReadConfigDataDir result + data opens:"
    grep -cE "openat.*(/mnt/plc/config/(channel|configfiles)|tnc.cfg|ChannelCfg)" /tmp/cfgsrv_cf.log 2>/dev/null
    echo "### IPO (fg, -k=NC) ###"
    LD_PRELOAD="$IPOPRE" timeout -s KILL 80 FEXInterpreter "$R/heros5/bin/ipo_progstation.elf" \
        -p=~/IPO IPO -k=NC -M > /tmp/ipo_cf.log 2>&1 || true
    echo "### IPO done ###"
    kill $CFGPID 2>/dev/null; wait $CFGPID 2>/dev/null
  '
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
G2=$(md5sum /etc/passwd | awk '{print $1}'); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"

echo ""
echo "=== ★ ConfigServer config load (data files opened?) ==="
sudo grep -aoE "openat\(AT_FDCWD, \"[^\"]*(channel|configfiles|tnc|ChannelCfg|GlobalSystem)[^\"]*\"" /tmp/cfgsrv_cf.log 2>/dev/null | sort -u | head
echo ""
echo "=== ★★ IPO outcome: 'Connected' (PASS) vs 'Invalid Command Option -k' (config not served) ==="
grep -aiE "connected|Invalid Command|CheckOptions|AskIpoConditions|-k|channel|condition|abort" /tmp/ipo_cf.log 2>/dev/null | grep -avE "^\[rtos\]|^\[t[0-9a-f]|FULL\[|Q_create|Q_ident|Q_send|Q_read" | head -20
echo "--- INJECT_ACK posted? ---"
grep -ac "INJECT_ACK" /tmp/ipo_cf.log 2>/dev/null
echo "logs: /tmp/cfgsrv_cf.log /tmp/ipo_cf.log"
