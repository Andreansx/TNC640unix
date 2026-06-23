set -u
REPO=/Users/andreansx/Documents/TNC640unix; CFG=$REPO/work/control/sysroot
R=/var/tmp/lr; CC=i686-linux-gnu-gcc
PRE="/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c || exit 1; done
# stage config + productid + writable dirs + jh_int + no-op encfs (the full ConfigServer fix set)
sudo mkdir -p /mnt/sys/config /mnt/plc/config /mnt/sys/cache/nckern/productid /mnt/tnc/config /mnt/plce/config
sudo cp -aL "$CFG/config/." /mnt/sys/config/ 2>/dev/null; sudo cp -aL "$CFG/config/." /mnt/tnc/config/ 2>/dev/null
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -aL "$CFG/default/oem/config/." /mnt/plc/config/ 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rwX /mnt/sys/config /mnt/plc/config /mnt/sys/cache /mnt/tnc /mnt/plce 2>/dev/null
printf "int main(void){return 0;}\n" > /tmp/noop.c; $CC -O2 -static -o /tmp/noop /tmp/noop.c 2>/dev/null
sudo cp /tmp/noop $R/usr/bin/encfs; sudo cp /tmp/noop $R/usr/bin/fusermount; sudo chmod 0755 $R/usr/bin/encfs $R/usr/bin/fusermount
sudo mkdir -p /mnt/sys/config/jh_int; sudo cp -aL /mnt/sys/config/*.cfg /mnt/sys/config/*.atr /mnt/sys/config/layout /mnt/sys/config/jh_int/ 2>/dev/null; sudo chmod -R a+rwX /mnt/sys/config/jh_int 2>/dev/null
SYSW=/var/tmp/sysw; sudo rm -rf "$SYSW"; sudo mkdir -p "$SYSW"; sudo cp -aL "$CFG/config" "$SYSW/config"; sudo cp -aL "$CFG/resource" "$SYSW/resource" 2>/dev/null; sudo chmod -R a+rwX "$SYSW"
OEMW=/var/tmp/oemw; sudo rm -rf "$OEMW"; sudo cp -aL "$CFG/default/oem" "$OEMW" 2>/dev/null; sudo chmod -R a+rwX "$OEMW" 2>/dev/null
ln -sfn "$SYSW" /tmp/s; ln -sfn "$OEMW" /tmp/o
[ -e /etc/jhvolume ] || sudo bash $REPO/emulator/setup_jhvolume.sh >/dev/null 2>&1 || true

for i in 1 2 3; do sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1; done
sudo rm -f /dev/shm/heros_* /dev/shm/_heusrv_shm 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'
sudo rm -f /tmp/cfgdl.log /tmp/cfgdl_strace.log
echo "=== ConfigServer (fallback runup_done on serve-loop -> INJECT_REREAD -> data load) ==="
sudo env R="$R" PRE="$PRE" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=$R/heros5/bin EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROS_FAKE_NS=1 MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
  HEROSCALL_VERBOSE=0 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0; mount --make-rprivate /; mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"
    timeout -s KILL 90 /usr/bin/strace -f -qq -e trace=openat -o /tmp/cfgdl_strace.log \
      env LD_PRELOAD="'"$PRE"'" FEXInterpreter $R/heros5/bin/ConfigServer.elf -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/cfgdl.log 2>&1
    pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null'
echo "=== RESULTS ==="
echo "★ serve-loop fallback fired: $(sudo grep -ac "serve-loop fallback" /tmp/cfgdl.log 2>/dev/null)"
echo "★ crash: $(sudo grep -ac "free():\|signal 6\|PciHardware" /tmp/cfgdl.log 2>/dev/null)"
echo "★ DATA-OPENS (the config #6 data load — target >0):"
echo "   tnc.cfg/channel.cfg/ChannelCfg.atr opens: $(sudo grep -acE "openat.*(tnc\.cfg|channel\.cfg|ChannelCfg\.atr|configfiles\.cfg)" /tmp/cfgdl_strace.log 2>/dev/null)"
echo "   total .cfg/.atr opens: $(sudo grep -acE "openat.*\.(cfg|atr)\"" /tmp/cfgdl_strace.log 2>/dev/null)"
sudo grep -aoE "openat\(AT_FDCWD, \"[^\"]*\.(cfg|atr)\"" /tmp/cfgdl_strace.log 2>/dev/null | grep -aiE "tnc|channel|kin|axlist|global" | sort -u | head -6
