set -u
REPO=/Users/andreansx/Documents/TNC640unix; CFG=$REPO/work/control/sysroot
R=/var/tmp/lr; CC=i686-linux-gnu-gcc
PRE="/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c || exit 1; done
# stage /mnt/sys + /mnt/plc + productid + SYSW(=/tmp/s, with resources, the run_hrmmi layout)
sudo mkdir -p /mnt/sys/config /mnt/plc/config /mnt/sys/cache/nckern/productid
sudo cp -aL "$CFG/config/." /mnt/sys/config/ 2>/dev/null
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -aL "$CFG/default/oem/config/." /mnt/plc/config/ 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rX /mnt/sys/config /mnt/plc/config /mnt/sys/cache 2>/dev/null
SYSW=/var/tmp/sysw; sudo rm -rf "$SYSW"; sudo mkdir -p "$SYSW/runtime"
sudo cp -aL "$CFG/config" "$SYSW/config"; sudo cp -aL "$CFG/resource" "$SYSW/resource" 2>/dev/null; sudo cp -aL "$CFG/batch" "$SYSW/batch" 2>/dev/null
sudo chmod -R a+rwX "$SYSW" 2>/dev/null; ln -sfn "$SYSW" /tmp/s 2>/dev/null
[ -e /etc/jhvolume ] || bash $REPO/emulator/setup_jhvolume.sh >/dev/null 2>&1 || true

run_one(){  # $1=label  $2=SYSLINK target  $3=extra-env
  local label="$1" syslink="$2" extra="$3"
  for i in 1 2 3; do sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1; done
  sudo rm -f /dev/shm/heros_* /dev/shm/_heusrv_shm 2>/dev/null
  sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'
  local LOG=/tmp/cfgbis_${label}.log; sudo rm -f $LOG
  sudo env R="$R" PRE="$PRE" SYSLINK="$syslink" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=$R/heros5/bin EXECBAT=/mnt/sys/batch/heros5 \
    SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
    HEROSCALL_VERBOSE=0 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
    HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 LANG=C LC_ALL=C $extra \
    unshare -m bash -c '
      set -u; ulimit -c 0; mount --make-rprivate /; mount --bind "$R/etc" /etc
      export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
      cd /; ln -sfn "$SYSLINK" "/%SYS%"; ln -sfn /mnt/plc "/%OEM%"; ln -sfn "$SYSLINK" "/%USR%"
      LD_PRELOAD="'"$PRE"'" timeout -s KILL 40 FEXInterpreter $R/heros5/bin/ConfigServer.elf -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > '"$LOG"' 2>&1
    '
  local crash=$(sudo grep -ac "free(): invalid pointer\|terminating signal 6\|corrupt" $LOG 2>/dev/null)
  local runup=$(sudo grep -ac "RUNUP_COMPLETE" $LOG 2>/dev/null)
  echo "  [$label] syslink=$syslink extra='$extra' -> crash=$crash RUNUP_COMPLETE=$runup"
}

echo "=== A) /%SYS%->/mnt/sys (the no-crash baseline) ==="
run_one A_mntsys /mnt/sys ""
echo "=== B) /%SYS%->/tmp/s SYSW+resources (the run_hrmmi layout) ==="
run_one B_sysw /tmp/s ""
echo "=== C) SYSW + HEROS_EVENTS_PIPE=1 (the run_hrmmi events backing) ==="
run_one C_sysw_pipe /tmp/s "HEROS_EVENTS_PIPE=1"
for i in 1 2 3; do sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1; done
echo "logs: /tmp/cfgbis_A_mntsys.log /tmp/cfgbis_B_sysw.log /tmp/cfgbis_C_sysw_pipe.log"
