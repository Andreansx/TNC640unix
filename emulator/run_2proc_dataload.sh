set -u
REPO=/Users/andreansx/Documents/TNC640unix; CFG=$REPO/work/control/sysroot
R=/var/tmp/lr; CC=i686-linux-gnu-gcc
PRE="/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c || exit 1; done
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
# ensure IPO closure
sudo bash -c 'SRC='"$CFG"'; R='"$R"'; declare -A S; SKIP="libc.so.6 libpthread.so.0 librt.so.1 libdl.so.2 libm.so.6 ld-linux.so.2 libresolv.so.2 libutil.so.1 libnsl.so.1"
fl(){ find "$SRC/heros5/bin" "$SRC/usr/lib" "$SRC/lib" -name "$1" 2>/dev/null|head -1; }
cc(){ local l="$1"; [ -n "${S[$l]:-}" ]&&return; S[$l]=1; case " $SKIP " in *" $l "*)return;;esac; local p; p=$(fl "$l"); [ -z "$p" ]&&return; local rel=${p#$SRC/}; if ! { [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"; }; then mkdir -p "$R/$(dirname "$rel")"; cp -aL "$p" "$R/$rel"; fi; for n in $(i686-linux-gnu-objdump -p "$p" 2>/dev/null|awk "/NEEDED/{print \$2}"); do cc "$n"; done; }
for n in $(i686-linux-gnu-objdump -p "$R/heros5/bin/ipo_progstation.elf" 2>/dev/null|awk "/NEEDED/{print \$2}"); do cc "$n"; done' 2>/dev/null

for i in 1 2 3; do sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1; done
sudo rm -f /dev/shm/heros_* /dev/shm/_heusrv_shm 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'
sudo rm -f /tmp/dl_cfg.log /tmp/dl_ipo.log
echo "=== 2-proc: ConfigServer (data load) + IPO (-k=NC) ==="
sudo env R="$R" PRE="$PRE" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=$R/heros5/bin EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROS_FAKE_NS=1 MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
  HEROSCALL_VERBOSE=0 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0; mount --make-rprivate /; mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"
    ( LD_PRELOAD="'"$PRE"'" timeout -s KILL 150 FEXInterpreter $R/heros5/bin/ConfigServer.elf -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/dl_cfg.log 2>&1 ) &
    i=0; while [ $i -lt 90 ]; do grep -q "serve-loop fallback" /tmp/dl_cfg.log 2>/dev/null && { echo "  ConfigServer data load triggered at ${i}*0.5s"; break; }; sleep 0.5; i=$((i+1)); done
    sleep 8
    echo "### IPO (-k=NC) ###"
    timeout -s KILL 70 env LD_PRELOAD="'"$PRE"'" FEXInterpreter $R/heros5/bin/ipo_progstation.elf -p=~/IPO IPO -k=NC -M > /tmp/dl_ipo.log 2>&1
    pkill -KILL -x FEXInterpreter 2>/dev/null'
echo "=== RESULTS ==="
echo "★ ConfigServer data load: $(sudo grep -acE "openat" /tmp/dl_cfg.log 2>/dev/null) ; serve-loop fallback: $(sudo grep -ac "serve-loop fallback" /tmp/dl_cfg.log 2>/dev/null)"
echo "★ IPO outcome: Connected=$(sudo grep -ac "Connected" /tmp/dl_ipo.log 2>/dev/null) ; Invalid-Command-k=$(sudo grep -ac "Invalid Command Option" /tmp/dl_ipo.log 2>/dev/null) (0=PASS)"
echo "★ IPO past -k=NC? (AskIpoConditions/CheckOptions/HwsMailslot/IpoSystemView):"
sudo grep -aoE "AskIpoConditions|CheckOptions|HwsMailslot|IpoSystemView|Invalid Command Option [^ ]*" /tmp/dl_ipo.log 2>/dev/null | sort | uniq -c | head
echo "  IPO last output:"; sudo grep -aivE "cannot be preloaded|ld.so|^\[rtos\]|^\[t[0-9]|FULL\[" /tmp/dl_ipo.log 2>/dev/null | tail -5
