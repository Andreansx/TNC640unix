set -u
# Fred.elf — the REAL Programming-screen operator MMI (channel "Ed", processName ~/mmi).
# PLib/libgui app (NOT Qt, NOT Guppy's GTK/Python), winmgr-managed X11 screens.
# Reuses the proven softkey-bar stack: ConfigServer(+cfgfix) + optional winmgr + optional skmgr.
# Toggles:  WINMGR=1  SKMGR=1  CM=1  (peers).  Default = standalone scout (ConfigServer + Fred).
# Batch (TNC640heros.txt, subsystem "Ed"): ~/mmi = Fred.elf  -i=Nc -k=NC -s=Sim -p=SIM -h=60000 -d=60
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
TGT=$REPO/work/target/rootfs
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
DISP="${XDISPLAY:-:99}"
USE_XVFB=1; case "$DISP" in :99|:0|:0.0) USE_XVFB=1;; *) USE_XVFB=0;; esac
CFGPRE="/lib/noopfree.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
MMIPRE="${MMI_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
SKPRE="${SK_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
FRED_BIN="${FRED_BIN:-Fred.elf}"
FRED_PROC="${FRED_PROC:-~/mmi}"
FRED_SHORT="${FRED_SHORT:-mmi}"
FRED_ARGS="${FRED_ARGS:--i=Nc -k=NC -s=Sim -p=SIM -h=60000 -d=60}"

echo "=== build preloads ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/nolimit.map -o $R/lib/nolimit.so $REPO/emulator/nolimit.c || exit 1
for s in herosapi_shim heros_rtos renamefix fexunmask noopfree guardfree skspy skforce wmstub; do
  [ -f $REPO/emulator/$s.c ] && { $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c -ldl || exit 1; }
done

SYSW=/var/tmp/sysw; sudo rm -rf "$SYSW"; sudo mkdir -p "$SYSW"
sudo cp -aL "$CFG/config" "$SYSW/config"; sudo cp -aL "$CFG/batch" "$SYSW/batch"; sudo cp -aL "$CFG/resource" "$SYSW/resource" 2>/dev/null
sudo chmod -R a+rwX "$SYSW"
OEMW=/var/tmp/oemw; sudo rm -rf "$OEMW"; sudo cp -aL "$CFG/default/oem" "$OEMW" 2>/dev/null; sudo chmod -R a+rwX "$OEMW" 2>/dev/null
ln -sfn "$SYSW" /tmp/s; ln -sfn "$OEMW" /tmp/o; ln -sfn $R/heros5/bin /tmp/b

sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sudo pkill -x Xvfb 2>/dev/null; sudo pkill -x openbox 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm 2>/dev/null
sudo mkdir -p /mnt/sys/config /mnt/plc/config /mnt/sys/cache/nckern/productid /mnt/tnc/config
sudo cp -aL "$CFG/config/." /mnt/sys/config/ 2>/dev/null; sudo cp -aL "$CFG/config/." /mnt/tnc/config/ 2>/dev/null
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -aL "$CFG/default/oem/config/." /mnt/plc/config/ 2>/dev/null
sudo mkdir -p /mnt/plc/service && sudo chmod -R a+rwX /mnt/plc/service
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rwX /mnt/sys/config /mnt/plc/config /mnt/sys/cache /mnt/tnc 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'

if [ "$USE_XVFB" = "1" ]; then
  echo "=== start Xvfb $DISP + openbox ==="
  Xvfb $DISP -screen 0 1280x1024x16 -nolisten tcp >/tmp/x2.log 2>&1 & sleep 2
  if [ -z "${NO_OPENBOX:-}" ]; then
    cat > /tmp/openbox-rc.xml <<'OBRC'
<?xml version="1.0" encoding="UTF-8"?>
<openbox_config xmlns="http://openbox.org/3.4/rc">
  <focus><focusNew>no</focusNew></focus>
  <applications>
    <application class="*"><decor>no</decor><focus>no</focus></application>
  </applications>
</openbox_config>
OBRC
    DISPLAY=$DISP openbox --config-file /tmp/openbox-rc.xml >/tmp/ob2.log 2>&1 & sleep 1
  fi
else
  echo "=== external X DISPLAY=$DISP (Mac XQuartz) ==="
fi

GUARD=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/f_cfg.log /tmp/f_sk.log /tmp/f_sk_strace.log /tmp/f_mmi.log /tmp/f_strace.log /tmp/f_cm.log /tmp/f_wm.log /tmp/f_wm_strace.log
sudo env R="$R" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_EVT_ACK=1 HEROSCALL_INJECT_PEER_ACK="${INJECT_PEER_ACK:-1}" HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 HEROS_EVENTS_PIPE=1 \
  HEROS_CFG_REPLY_ROUTE=1 HEROSCALL_PIDENT_SELF="${PIDENT_SELF:-1}" HEROSCALL_SK_REPLY_FORCE="${SK_REPLY_FORCE:-1}" HEROSCALL_DUMPQ="${DUMPQ:-0}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_CAPTURE_TYPE="${CAPTURE_TYPE:-}" \
  HEROSCALL_INJECT_WMGR_ACK="${INJECT_WMGR_ACK:-0}" HEROSCALL_AREA_RECT_FORCE="${AREA_RECT_FORCE:-0}" DISP="$DISP" \
  CFGPRE="$CFGPRE" MMIPRE="$MMIPRE" SKPRE="$SKPRE" USE_XVFB="$USE_XVFB" \
  WINMGR="${WINMGR:-0}" SKMGR="${SKMGR:-0}" CM="${CM:-0}" WM_LAYOUT="${WM_LAYOUT:-}" WM_SIZE="${WM_SIZE:-}" WM_VERBOSE="${WM_VERBOSE:-1}" SYSFIRE="${SYSFIRE:-0}" WM_FIRE_LIMIT="${WM_FIRE_LIMIT:-0}" WM_FIRE_MASK="${WM_FIRE_MASK:-00ff0000}" WMGR_SCREEN="${WMGR_SCREEN:-0}" \
  FRED_BIN="$FRED_BIN" FRED_PROC="$FRED_PROC" FRED_SHORT="$FRED_SHORT" FRED_ARGS="$FRED_ARGS" SK_ARGS="${SK_ARGS:--w -k}" \
  SHOT_AT="${SHOT_AT:-100}" MMI_TIMEOUT="${MMI_TIMEOUT:-150}" LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /; mount --bind "$R/etc" /etc
    grep -q "127.0.0.1" /etc/hosts 2>/dev/null || printf "127.0.0.1\tlocalhost\n::1\tlocalhost\n" > /etc/hosts
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu DISPLAY=$DISP HEROSROOT=$R/heros5
    mkdir -p /etc/fonts; [ -e /etc/fonts/fonts.conf ] || printf "<?xml version=\"1.0\"?><fontconfig><dir>/usr/share/fonts</dir><cachedir>/tmp/fc</cachedir></fontconfig>" > /etc/fonts/fonts.conf
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"
    # FResMgr in libfrontend resolves the frontend resource as /frontend.dat with an empty base, so ENOENT.
    # Provide it at root base plus 1280 sized variant so the PLib frontend resource bundle loads.
    for fd in frontend.dat frontend1280.dat mmiPrg1280.dat mmiOnlG1280.dat mmiMan1280.dat Fred1280.dat; do
      [ -e /tmp/s/resource/$fd ] && ln -sfn /tmp/s/resource/$fd /$fd; done

    echo "### ConfigServer (bg) ###"
    ( env HEROS_FAKE_NS=1 HEROSCALL_VERBOSE=1 MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
        LD_PRELOAD="$CFGPRE" timeout -s KILL 300 FEXInterpreter "$R/heros5/bin/ConfigServer.elf" \
        -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/f_cfg.log 2>&1 ) &
    CFGPID=$!
    i=0; while [ $i -lt 200 ]; do
      grep -qE "HWS stub: replied|RUNUP_COMPLETE" /tmp/f_cfg.log 2>/dev/null && { echo "  ConfigServer run-up at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    sleep 8

    if [ "${WINMGR:-0}" = "1" ]; then
      echo "### winmgr (bg) ###"
      WM_LAYOUT="${WM_LAYOUT:-%SYS%/resource/tnc640layout1280.xml}"; WM_SIZE="${WM_SIZE:-1280x1024}"
      ( env HEROSCALL_VERBOSE="${WM_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_SYSEVENT_AUTOFIRE="${SYSFIRE:-0}" HEROSCALL_SYSEVENT_FIRE_LIMIT="${WM_FIRE_LIMIT:-0}" HEROSCALL_SYSEVENT_FIRE_MASK="${WM_FIRE_MASK:-00ff0000}" HEROSCALL_WMGR_SCREEN="${WMGR_SCREEN:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
          LD_PRELOAD="$SKPRE" timeout -s KILL 300 /usr/bin/strace -f -qq -e trace=connect,writev -o /tmp/f_wm_strace.log \
          FEXInterpreter "$R/heros5/bin/winmgr.elf" -p=~/winmgr winmgr -m=5 -i=$WM_LAYOUT -o=afk -s=$WM_SIZE \
          -k=%SYS%/resource/keymap_us101.xml -c=%SYS%/resource/charmap_us101.xml -f=%SYS%/resource/functionkeymap_us101.xml \
          > /tmp/f_wm.log 2>&1 ) &
      WMPID=$!
      i=0; while [ $i -lt 160 ]; do grep -qaE "Q_create .Q_WMGR" /tmp/f_wm.log 2>/dev/null && { echo "  winmgr Q_WMGR at ${i}*0.5s"; break; }; sleep 0.5; i=$((i+1)); done
      sleep 6
    fi

    if [ "${SKMGR:-0}" = "1" ]; then
      echo "### skmgr (bg) ###"
      ( env HEROSCALL_VERBOSE=1 HEROSCALL_HSTRACE="${HSTRACE:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
          LD_PRELOAD="$SKPRE" timeout -s KILL 300 /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/f_sk_strace.log \
          FEXInterpreter "$R/heros5/bin/skmgr.elf" -p=~/skmgr skmgr $SK_ARGS > /tmp/f_sk.log 2>&1 ) &
      SKPID=$!
      i=0; while [ $i -lt 120 ]; do grep -qE "Q_create .Q_SkMgrCtrl" /tmp/f_sk.log 2>/dev/null && { echo "  skmgr Q_SkMgr at ${i}*0.5s"; break; }; sleep 0.5; i=$((i+1)); done
      sleep 6
    fi

    if [ "${CM:-0}" = "1" ]; then
      echo "### ChannelManager (bg, -k=SIM) ###"
      ( env HEROSCALL_VERBOSE=1 HEROSCALL_HSTRACE="${HSTRACE:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
          LD_PRELOAD="$MMIPRE" timeout -s KILL 300 FEXInterpreter "$R/heros5/bin/ChannelManager.elf" -p=~/CM CM -k=SIM > /tmp/f_cm.log 2>&1 ) &
      CMPID=$!
      sleep 6
    fi

    for p in pystdout pystderr ncstdout ncstderr; do
      rm -f /tmp/__helogpipe_$p; mkfifo /tmp/__helogpipe_$p 2>/dev/null
      ( timeout "${MMI_TIMEOUT:-150}" cat /tmp/__helogpipe_$p > /tmp/f_$p.log 2>/dev/null & )
    done

    echo "### Fred ($FRED_BIN -p=$FRED_PROC $FRED_SHORT $FRED_ARGS, DISPLAY=$DISP) ###"
    [ "$USE_XVFB" = "1" ] && ( sleep "${SHOT_AT:-100}"; rm -f /tmp/f_screen.xwd;
      DISPLAY=$DISP xwininfo -root -tree > /tmp/f_windows.txt 2>&1;
      DISPLAY=$DISP xwd -root -out /tmp/f_screen.xwd 2>/dev/null && echo "  screenshot bytes: $(wc -c </tmp/f_screen.xwd 2>/dev/null)" ) &
    timeout -s KILL "${MMI_TIMEOUT:-150}" /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/f_strace.log \
      env DISPLAY="$DISP" HEROSCALL_VERBOSE="${MMI_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_DUMPQ="${DUMPQ:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 LD_PRELOAD="$MMIPRE" \
      FEXInterpreter "$R/heros5/bin/$FRED_BIN" -p=$FRED_PROC $FRED_SHORT $FRED_ARGS > /tmp/f_mmi.log 2>&1 || true
    echo "### Fred done ###"
    pkill -KILL -x strace 2>/dev/null; kill $CFGPID ${SKPID:-} ${WMPID:-} ${CMPID:-} 2>/dev/null
  '
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
G2=$(md5sum /etc/passwd | awk '{print $1}'); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"
echo ""
echo "=== Fred scout result ==="
echo "  ConfigServer: lines=$(sudo wc -l </tmp/f_cfg.log 2>/dev/null) crash=$(sudo grep -acE "signal 6|signal 11|terminating signal" /tmp/f_cfg.log 2>/dev/null)"
echo "  Fred: lines=$(sudo wc -l </tmp/f_mmi.log 2>/dev/null) crash=$(sudo grep -acE "signal 6|signal 11|terminating signal" /tmp/f_mmi.log 2>/dev/null)"
echo "  Fred config connect (Connected): $(sudo grep -acE "Connected|OnCfgClientIsConnected" /tmp/f_mmi.log 2>/dev/null)"
echo "  Fred queues created: $(sudo grep -acE "Q_create" /tmp/f_mmi.log 2>/dev/null)  Q_ident: $(sudo grep -acE "Q_ident" /tmp/f_mmi.log 2>/dev/null)"
echo "  Fred X11 connect: $(sudo grep -acE "X11-unix" /tmp/f_strace.log 2>/dev/null)  writev->X: $(sudo grep -acE "writev\(" /tmp/f_strace.log 2>/dev/null)"
echo "  Fred Ev_receive blocks (forever): $(sudo grep -aoE "Ev_receive\(0x[0-9a-f]+, .*forever|Ev_receive.*0x[0-9a-f]+" /tmp/f_mmi.log 2>/dev/null | tail -5 | tr '\n' '|')"
echo "  Xvfb client windows: $(DISPLAY=$DISP xwininfo -root -children 2>/dev/null | grep -cE "0x[0-9a-f]+ \"")"
echo "  screenshot unique colours (1=blank): $(DISPLAY=$DISP convert /tmp/f_screen.xwd -format "%k" info: 2>/dev/null || echo n/a)"
echo "  --- Fred last 25 log lines ---"; sudo tail -25 /tmp/f_mmi.log 2>/dev/null
echo "logs: /tmp/f_cfg.log /tmp/f_mmi.log /tmp/f_strace.log (+ f_wm.log f_sk.log f_cm.log if peers on)"
