set -u
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
R=/var/tmp/lr; CC=i686-linux-gnu-gcc; DISP=:99
PRE="/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== build preloads + stage HrMmi closure ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos renamefix fexunmask; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c || exit 1; done
# ensure HrMmi's i386 closure is in the rootfs (cp -aL; mostly overlaps the AppStartMP closure)
sudo bash -c '
SRC='"$CFG"'; R='"$R"'; declare -A S; SKIP="libc.so.6 libpthread.so.0 librt.so.1 libdl.so.2 libm.so.6 ld-linux.so.2 libresolv.so.2 libutil.so.1 libnsl.so.1"
fl(){ find "$SRC/heros5/bin" "$SRC/usr/lib" "$SRC/lib" -name "$1" 2>/dev/null|head -1; }
cc(){ local l="$1"; [ -n "${S[$l]:-}" ]&&return; S[$l]=1; case " $SKIP " in *" $l "*)return;;esac
  local p; p=$(fl "$l"); [ -z "$p" ]&&return; local rel=${p#$SRC/}
  if ! { [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"; }; then mkdir -p "$R/$(dirname "$rel")"; cp -aL "$p" "$R/$rel"; fi
  for n in $(i686-linux-gnu-objdump -p "$p" 2>/dev/null|awk "/NEEDED/{print \$2}"); do cc "$n"; done; }
for n in $(i686-linux-gnu-objdump -p "$R/heros5/bin/HrMmi.elf" 2>/dev/null|awk "/NEEDED/{print \$2}"); do cc "$n"; done
echo "  HrMmi closure ensured (${#S[@]} nodes)"'

# writable SYS mirror with resources (PLIB++ keymap/charmap) + config + batch
SYSW=/var/tmp/sysw; sudo rm -rf "$SYSW"; sudo mkdir -p "$SYSW/runtime"
sudo cp -aL "$CFG/config" "$SYSW/config"; sudo cp -aL "$CFG/batch" "$SYSW/batch"; sudo cp -aL "$CFG/resource" "$SYSW/resource" 2>/dev/null
sudo chmod -R u+w "$SYSW/runtime"
ln -sfn "$SYSW" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
# config-#6 prerequisites (cfgfix needs the volume targets + productid)
sudo mkdir -p /mnt/sys/config /mnt/plc/config /mnt/sys/cache/nckern/productid
sudo cp -aL "$CFG/config/." /mnt/sys/config/ 2>/dev/null
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -aL "$CFG/default/oem/config/." /mnt/plc/config/ 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rX /mnt/sys/config /mnt/plc/config /mnt/sys/cache

sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sudo pkill -x Xvfb 2>/dev/null; sudo pkill -x openbox 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'
echo "=== start Xvfb $DISP + openbox ==="
Xvfb $DISP -screen 0 1280x1024x16 -nolisten tcp >/tmp/xvfb.log 2>&1 & sleep 2
DISPLAY=$DISP openbox >/tmp/openbox.log 2>&1 & sleep 2
echo "  X socket: $(ls /tmp/.X11-unix/ 2>/dev/null)"

GUARD=$(md5sum /etc/passwd|awk '{print $1}')
sudo rm -f /tmp/hrmmi_cfgsrv.log /tmp/hrmmi.log /tmp/hrmmi_strace.log
sudo env R="$R" PRE="$PRE" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ DISP="$DISP" \
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 HEROS_EVENTS_PIPE=1 \
  LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0; mount --make-rprivate /; mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu DISPLAY=$DISP HEROSROOT=$R/heros5
    mkdir -p /etc/fonts; [ -e /etc/fonts/fonts.conf ] || printf "<?xml version=\"1.0\"?><fontconfig><dir>/usr/share/fonts</dir><cachedir>/tmp/fc</cachedir></fontconfig>" > /etc/fonts/fonts.conf
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"
    echo "### ConfigServer (bg, cfgfix) ###"
    ( LD_PRELOAD="$PRE" timeout -s KILL 150 FEXInterpreter $R/heros5/bin/ConfigServer.elf -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/hrmmi_cfgsrv.log 2>&1 ) &
    echo "  cfgsrv early log:"; sleep 3; head -8 /tmp/hrmmi_cfgsrv.log 2>/dev/null | grep -aviE "cannot be preloaded"; i=0; while [ $i -lt 120 ]; do grep -q "HWS stub: replied" /tmp/hrmmi_cfgsrv.log 2>/dev/null && { echo "  ConfigServer run-up done at ${i}*0.5s"; break; }; sleep 0.5; i=$((i+1)); done
    sleep 5
    echo "### HrMmi.elf (fg, -k=NC, DISPLAY=$DISP) — the Qt/PLIB++ MMI ###"
    timeout -s KILL 75 /usr/bin/strace -f -qq -e trace=openat,connect,execve -o /tmp/hrmmi_strace.log \
      env LD_PRELOAD="$PRE" FEXInterpreter $R/heros5/bin/HrMmi.elf -p=~/hrmmi hrmmi -k=NC > /tmp/hrmmi.log 2>&1
    pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null
  '
G2=$(md5sum /etc/passwd|awk "{print \$1}"); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"
echo ""
echo "=== ★ did HrMmi CONNECT to X (Xvfb client)? ==="
grep -aiE "client|connect" /tmp/xvfb.log 2>/dev/null | tail -3
sudo grep -aE "connect\(.*X11|/tmp/.X11-unix" /tmp/hrmmi_strace.log 2>/dev/null | head -2
echo "=== ★★ HrMmi output: how far did it get? (RTOS / config / PLIB++ / X / render / blocker) ==="
grep -aivE "^\[rtos\]|^\[t[0-9]|cannot be preloaded|ld.so" /tmp/hrmmi.log 2>/dev/null | tail -30
echo "=== HrMmi RTOS last calls (where it blocks) ==="
sudo grep -aE "^\[t[0-9]+ hc|^\[rtos\]" /tmp/hrmmi.log 2>/dev/null | tail -6
echo "logs: /tmp/hrmmi.log /tmp/hrmmi_cfgsrv.log /tmp/hrmmi_strace.log /tmp/xvfb.log"
