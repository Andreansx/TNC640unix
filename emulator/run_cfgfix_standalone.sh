set -u
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
PRE="/lib/cfgprobe.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== build preloads (cfgprobe logging interposer) ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgprobe.so $REPO/emulator/cfgprobe.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos renamefix; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c || exit 1; done

ln -sfn $CFG /tmp/s; ln -sfn $CFG/default/oem /tmp/o; ln -sfn $R/heros5/bin /tmp/b
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /tmp/cfgprobe.log 2>/dev/null
sudo mkdir -p /mnt/sys/cache/nckern/productid
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do
  printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+r /mnt/sys/cache
head -c 1048576 /dev/zero > /tmp/heusrv_shm; sudo cp /tmp/heusrv_shm /dev/shm/_heusrv_shm; sudo chmod 666 /dev/shm/_heusrv_shm

GUARD=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/cfgr3.log /tmp/cfgr3_strace.log
sudo env R="$R" PRE="$PRE" CFGPROBE_FIX_SYSFILE="${FIX:-}" \
  SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: \
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 \
  HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 \
  LANG=C LC_ALL=C LD_PRELOAD="$PRE" \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /; mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
    cd /
    timeout -s KILL 70 /usr/bin/strace -f -qq -e trace=openat -o /tmp/cfgr3_strace.log \
      FEXInterpreter "$R/heros5/bin/ConfigServer.elf" Server:Server/cfgserver \
        -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/cfgr3.log 2>&1
    pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null
  '
G2=$(md5sum /etc/passwd | awk '{print $1}'); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"

echo ""
echo "=== config-load trace (FIX=${FIX:-off}): IsSysFile / ReadConfigDataDir / MissingFile ==="
sudo grep -anE "ReadConfigDataSet|ReadConfigDataDir|IsSysFile|IsOemFile|ReadMessage|IsForbidden|MissingFile" /tmp/cfgprobe.log 2>/dev/null | grep -avE "layout|version.cfg|plce.zip" | head -30
echo ""
echo "=== ★ CASCADE: reached the OEM index + data files? (configfiles.cfg / channel.cfg / tnc.cfg) ==="
sudo grep -aE "configfiles\.cfg|channel\.cfg|tnc\.cfg|axlist|ChannelCfg|GlobalSystem|kin\.cfg" /tmp/cfgr3_strace.log 2>/dev/null | grep -aE "openat" | sed -E 's/.*openat\(//' | sort -u | head -25
echo "    total OEM/data opens: $(sudo grep -acE 'configfiles\.cfg|channel\.cfg|tnc\.cfg|ChannelCfg|GlobalSystem|axlist|kin\.cfg' /tmp/cfgr3_strace.log 2>/dev/null)"
echo ""
echo "=== ConfigServer stdout (Connected / config read / errors) ==="
grep -aiE "missing|invalid|connected|configfiles|channel|read cycle|config.*read|datafiles" /tmp/cfgr3.log 2>/dev/null | grep -avE "ld.so|cannot be preloaded|^\[t" | head-15 2>/dev/null || grep -aiE "missing|invalid|connected|configfiles|channel|read cycle|datafiles" /tmp/cfgr3.log 2>/dev/null | grep -avE "ld.so|cannot be preloaded|^\[t" | head -15
