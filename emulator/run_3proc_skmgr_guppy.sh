set -u
# 3-PROC: ConfigServer + skmgr (SkManager) + Guppy (HwViewer) under FEX.
# Goal: skmgr owns Q_SkMgr/Q_SkMgrCtrl so Guppy's GUPPYSKMGR client (libSkMgrCtrl) finds it,
# the jh.softkey.Register bind succeeds, and the softkey bar fills with buttons.
# Extends run_guppy_window.sh (the HwViewer render) by inserting skmgr between ConfigServer and Guppy.
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
TGT=$REPO/work/target/rootfs
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
DISP="${XDISPLAY:-:99}"
# gtk_external_display_active() (libgtkbind 0x16510) returns TRUE unless the X display is exactly ":0.0";
# when TRUE, GUPPYRUNTIMEGTK_::CreateWindowData SKIPS the screen->Wnd-type dispatch -> the window is NOT
# bind-capable -> jh.softkey.Register fails "Binding...failed". So run Xvfb on :0 (gdk -> ":0.0") for a
# bind-capable OEM window (faithful, no patch). Start our own Xvfb for :99/:0/:0.0; else external X.
USE_XVFB=1; case "$DISP" in :99|:0|:0.0) USE_XVFB=1;; *) USE_XVFB=0;; esac
CFGPRE="/lib/noopfree.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
MMIPRE="${MMI_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
[ "${SKCONNFORCE:-0}" = "1" ] && MMIPRE="/lib/skconnforce.so:$MMIPRE"
SKPRE="${SK_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
GUPPY_BIN="${GUPPY_BIN:-Guppy.elf}"
GUPPY_C="${GUPPY_C:-HwSetup}"
GUPPY_ARGS="-R=UnloadHwSetup -v=c -C=$GUPPY_C -i=Nc -s=Sim"
SK_ARGS="${SK_ARGS:--w -k}"

echo "=== build preloads ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/nolimit.map -o $R/lib/nolimit.so $REPO/emulator/nolimit.c || exit 1
for s in herosapi_shim heros_rtos renamefix fexunmask noopfree guardfree skspy skforce skconnforce wmstub gdactive; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c -ldl || exit 1; done

echo "=== stage Python runtime (idempotent) ==="
if [ "${STAGE:-0}" = "1" ] || [ ! -d "$R/usr/lib/python2.7" ]; then
  sudo mkdir -p $R/usr/lib $R/usr/bin
  sudo cp -a $TGT/usr/lib/python2.7 $R/usr/lib/python2.7
  sudo cp -a $TGT/usr/bin/python2.7 $R/usr/bin/python2.7 2>/dev/null
  sudo mkdir -p $R/usr/lib/python/site-packages
  sudo cp -a $CFG/usr/lib/python/site-packages/. $R/usr/lib/python/site-packages/
  sudo cp -a $TGT/usr/lib/gdk-pixbuf-2.0 $R/usr/lib/gdk-pixbuf-2.0
else
  echo "  already staged"
fi

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
sudo mkdir -p /mnt/sys/Python /mnt/sys/usr/lib/python
sudo cp -aL "$CFG/Python/." /mnt/sys/Python/ 2>/dev/null
sudo cp -aL "$CFG/usr/lib/python/site-packages" /mnt/sys/usr/lib/python/site-packages 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rwX /mnt/sys/config /mnt/plc/config /mnt/sys/cache /mnt/tnc /mnt/sys/Python /mnt/sys/usr 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'

if [ "$USE_XVFB" = "1" ]; then
  echo "=== start Xvfb $DISP${NO_OPENBOX:+ (no openbox)}${NO_OPENBOX:- + openbox} ==="
  Xvfb $DISP -screen 0 1280x1024x16 -nolisten tcp >/tmp/x2.log 2>&1 & sleep 2
  # The real TNC WM (winmgr) places the WndFullScreen at the OEM-screen rect (0,0 fullscreen) WITHOUT a
  # frame. A stock openbox instead DECORATES + reparents the OEM window: HwViewer.py:GetDecorationSize then
  # reads bogus _NET_FRAME_EXTENTS (decoration=(1280,0,0,1)) -> width 1280-1280=0 -> the window collapses to
  # its glade natural size (~330x165) -> no room for the 88px softkey bar (the bar strip stays blank). skmgr
  # REQUIRES a running WM (its PLIB++ "waiting for X-WindowManager" aborts without one), so we cannot drop
  # openbox (NO_OPENBOX=1). Instead run openbox with an UNDECORATE rule (decor=no for every app): frame
  # extents become 0 -> GetDecorationSize returns ~0 -> the width calc is correct -> the OEM window maps at
  # the requested 1280x936 (and the separate 1280x88 softkey-bar window at y=936) = the real winmgr's
  # frameless fullscreen placement. NO_OPENBOX=1 still skips it entirely (but then skmgr aborts).
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
sudo rm -f /tmp/g_cfg.log /tmp/sk.log /tmp/sk_strace.log /tmp/g_mmi.log /tmp/g_strace.log
sudo env R="$R" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_EVT_ACK=1 HEROSCALL_INJECT_PEER_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 HEROS_EVENTS_PIPE=1 \
  HEROSCALL_INJECT_WMGR_ACK="${INJECT_WMGR_ACK:-0}" EMPTYPOLL_DIAG="${EMPTYPOLL_DIAG:-0}" EMPTYPOLL_YIELD="${EMPTYPOLL_YIELD:-0}" WMGR_SCREEN="${WMGR_SCREEN:-0}" WMQ_BREAK="${WMQ_BREAK:-0}" WMQ_BREAK_N="${WMQ_BREAK_N:-2000}" \
  HEROS_CFG_REPLY_ROUTE=1 HEROS_EV_SIGWAKE="${EV_SIGWAKE:-0}" HEROSCALL_PIDENT_SELF="${PIDENT_SELF:-1}" HEROSCALL_SK_REPLY_FORCE="${SK_REPLY_FORCE:-1}" HEROSCALL_DUMPQ="${DUMPQ:-0}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_CAPTURE_TYPE="${CAPTURE_TYPE:-}" HEROSCALL_INJECT_SK_FLOW="${INJECT_SK_FLOW:-0}" HEROSCALL_INJECT_AREA_ACK="${INJECT_AREA_ACK:-0}" DISP="$DISP" \
  HEROSCALL_INJECT_SK_ACTIVATE="${INJECT_SK_ACTIVATE:-0}" HEROSCALL_SK_ACT_THRESH="${SK_ACT_THRESH:-5}" HEROSCALL_SK_ACT_SCREEN="${SK_ACT_SCREEN:-}" HEROSCALL_SK_ACT_GROUP="${SK_ACT_GROUP:-0}" HEROSCALL_SK_ACT_HANDLE="${SK_ACT_HANDLE:-13}" HEROSCALL_SK_ACT_BOOL="${SK_ACT_BOOL:-0}" \
  HEROSCALL_AREA_RECT_FORCE="${AREA_RECT_FORCE:-0}" \
  HEROSCALL_EV_TRACE_BIT="${EV_TRACE_BIT:-0}" HEROSCALL_EV_TRACE_TASK="${EV_TRACE_TASK:-0}" HEROSCALL_EV_TRACE_BUDGET="${EV_TRACE_BUDGET:-24}" HEROSCALL_EV_TRACE_EXACT="${EV_TRACE_EXACT:-0}" \
  CFGPRE="$CFGPRE" MMIPRE="$MMIPRE" SKPRE="$SKPRE" USE_XVFB="$USE_XVFB" NO_NCK_WINMGR="${NO_NCK_WINMGR:-}" WMFORCE="${WMFORCE:-}" SKFORCE="${SKFORCE:-}" \
  HWVIEWER_SK_USAGE="${HWVIEWER_SK_USAGE:-}" HWVIEWER_GEOMETRY="${HWVIEWER_GEOMETRY:-}" WINMGR="${WINMGR:-0}" WM_LAYOUT="${WM_LAYOUT:-}" WM_SIZE="${WM_SIZE:-}" WM_VERBOSE="${WM_VERBOSE:-1}" SYSFIRE="${SYSFIRE:-0}" SYSFIRE_MASK="${SYSFIRE_MASK:-00ff0000}" WM_FIRE_LIMIT="${WM_FIRE_LIMIT:-0}" WM_FIRE_YIELD="${WM_FIRE_YIELD:-0}" \
  GUPPY_BIN="$GUPPY_BIN" GUPPY_ARGS="$GUPPY_ARGS" GUPPY_C="$GUPPY_C" SK_ARGS="$SK_ARGS" GUPPY_DISPLAY="${GUPPY_DISPLAY:-}" HWV_FORCE_FS="${HWV_FORCE_FS:-}" LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /; mount --bind "$R/etc" /etc
    grep -q "127.0.0.1" /etc/hosts 2>/dev/null || printf "127.0.0.1\tlocalhost\n::1\tlocalhost\n" > /etc/hosts
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu DISPLAY=$DISP HEROSROOT=$R/heros5
    export PYTHONHOME=/usr
    mkdir -p /etc/fonts; [ -e /etc/fonts/fonts.conf ] || printf "<?xml version=\"1.0\"?><fontconfig><dir>/usr/share/fonts</dir><cachedir>/tmp/fc</cachedir></fontconfig>" > /etc/fonts/fonts.conf
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"

    echo "### ConfigServer (bg) ###"
    ( env HEROS_FAKE_NS=1 HEROSCALL_VERBOSE=1 MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
        LD_PRELOAD="$CFGPRE" timeout -s KILL 300 FEXInterpreter "$R/heros5/bin/ConfigServer.elf" \
        -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/g_cfg.log 2>&1 ) &
    CFGPID=$!
    i=0; while [ $i -lt 200 ]; do
      grep -qE "HWS stub: replied|RUNUP_COMPLETE" /tmp/g_cfg.log 2>/dev/null && { echo "  ConfigServer run-up at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    sleep 8

    if [ "${WINMGR:-0}" = "1" ]; then
      echo "### winmgr (bg, the window manager — owns Q_WMGR; serves skmgr+Guppy WM handshake) ###"
      WM_LAYOUT="${WM_LAYOUT:-%SYS%/resource/tnc640layout1280.xml}"
      WM_SIZE="${WM_SIZE:-1280x1024}"
      ( env HEROSCALL_VERBOSE="${WM_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_SYSEVENT_AUTOFIRE="${SYSFIRE:-0}" HEROSCALL_SYSEVENT_FIRE_MASK="${SYSFIRE_MASK:-00ff0000}" HEROSCALL_SYSEVENT_FIRE_LIMIT="${WM_FIRE_LIMIT:-0}" HEROSCALL_SYSEVENT_FIRE_YIELD_US="${WM_FIRE_YIELD:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
          LD_PRELOAD="$SKPRE" timeout -s KILL 300 /usr/bin/strace -f -qq -e trace=connect,writev -o /tmp/wm_strace.log \
          FEXInterpreter "$R/heros5/bin/winmgr.elf" -p=~/winmgr winmgr \
          -m=5 -i=$WM_LAYOUT -o=afk -s=$WM_SIZE \
          -k=%SYS%/resource/keymap_us101.xml -c=%SYS%/resource/charmap_us101.xml -f=%SYS%/resource/functionkeymap_us101.xml \
          > /tmp/wm.log 2>&1 ) &
      WMPID=$!
      i=0; while [ $i -lt 160 ]; do
        grep -qaE "Q_create .Q_WMGR" /tmp/wm.log 2>/dev/null && { echo "  winmgr created Q_WMGR at ${i}*0.5s"; break; }
        sleep 0.5; i=$((i+1)); done
      sleep 6
    fi

    echo "### skmgr (bg, -p=~/skmgr skmgr $SK_ARGS) ###"
    ( env HEROSCALL_VERBOSE="${SK_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_EMPTYPOLL_DIAG="${EMPTYPOLL_DIAG:-0}" HEROSCALL_EMPTYPOLL_YIELD="${EMPTYPOLL_YIELD:-0}" HEROSCALL_WMGR_SCREEN="${WMGR_SCREEN:-0}" HEROSCALL_WMQ_BREAK="${WMQ_BREAK:-0}" HEROSCALL_WMQ_BREAK_N="${WMQ_BREAK_N:-2000}" HEROSCALL_SELECT_CAP_MS="${SELECT_CAP_MS:-0}" HEROSCALL_PPOLL_DBG="${PPOLL_DBG:-0}" HEROSCALL_SYSEVENT_AUTOFIRE="${SK_SYSFIRE:-0}" HEROSCALL_SYSEVENT_FIRE_MASK="${SK_SYSFIRE_MASK:-00010000}" HEROSCALL_EV_TIMEOUT_CAP_MS="${SK_EVCAP:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
        LD_PRELOAD="$SKPRE" timeout -s KILL 300 /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/sk_strace.log \
        FEXInterpreter "$R/heros5/bin/skmgr.elf" -p=~/skmgr skmgr $SK_ARGS > /tmp/sk.log 2>&1 ) &
    SKPID=$!
    i=0; while [ $i -lt 120 ]; do
      grep -qE "Q_create .Q_SkMgrCtrl" /tmp/sk.log 2>/dev/null && { echo "  skmgr created Q_SkMgr/Q_SkMgrCtrl at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    sleep "${GUPPY_DELAY:-6}"

    for p in pystdout pystderr ncstdout ncstderr; do
      rm -f /tmp/__helogpipe_$p; mkfifo /tmp/__helogpipe_$p 2>/dev/null
      ( timeout "${MMI_TIMEOUT:-200}" cat /tmp/__helogpipe_$p > /tmp/g_$p.log 2>/dev/null & )
    done

    GDISP="${GUPPY_DISPLAY:-$DISP}"   # faithful bind: gtk_external_display_active() (libgtkbind 0x16510) returns FALSE only when gdk_get_display()==":0.0" EXACTLY; then CreateWindowData runs the WndFullScreen dispatch (sets window-record +0x1C) so jh.softkey.Register binds -> login q_sends to Q_SkMgr -> skmgr serves. A plain ":0" gives gdk_get_display()==":0" -> external active -> dispatch skipped -> "Binding softkey resource to window failed". So promote :0 -> :0.0 for Guppy (Xvfb still on :0). NO binary patch, proper window geometry.
    case "$GDISP" in :0) GDISP=:0.0;; esac
    echo "### Guppy ($GUPPY_BIN $GUPPY_ARGS, DISPLAY=$GDISP) ###"
    [ "$USE_XVFB" = "1" ] && ( sleep "${SHOT_AT:-150}"; rm -f /tmp/g_screen.xwd; DISPLAY=$DISP xwininfo -root -tree > /tmp/g_windows.txt 2>&1; DISPLAY=$DISP xwd -root -out /tmp/g_screen.xwd 2>/dev/null && echo "  screenshot bytes: $(wc -c </tmp/g_screen.xwd 2>/dev/null)" ) &
    timeout -s KILL "${MMI_TIMEOUT:-200}" /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/g_strace.log \
      env DISPLAY="$GDISP" HEROSCALL_VERBOSE="${MMI_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 PYTHONHOME=/usr NO_NCK_WINMGR="$NO_NCK_WINMGR" WMFORCE="$WMFORCE" SKFORCE="$SKFORCE" HWVIEWER_SK_USAGE="$HWVIEWER_SK_USAGE" HWVIEWER_GEOMETRY="$HWVIEWER_GEOMETRY" HEROSCALL_WMQ_BREAK="${WMQ_BREAK:-0}" HEROSCALL_WMQ_BREAK_N="${WMQ_BREAK_N:-2000}" HEROSCALL_EMPTYPOLL_YIELD="${EMPTYPOLL_YIELD:-0}" HEROSCALL_WMGR_ROOT="${WMGR_ROOT:-0}" HEROSCALL_WMGR_SCREEN="${WMGR_SCREEN:-0}" HWV_FORCE_FS="${HWV_FORCE_FS:-}" HEROSCALL_EV_NOWAIT_YIELD="${EV_NOWAIT_YIELD:-0}" HEROSCALL_EV_NOWAIT_YIELD_N="${EV_NOWAIT_YIELD_N:-64}" LD_PRELOAD="$MMIPRE" \
      FEXInterpreter "$R/heros5/bin/$GUPPY_BIN" -p=~/Guppy Guppy $GUPPY_ARGS > /tmp/g_mmi.log 2>&1 || true
    echo "### Guppy done ###"
    pkill -KILL -x strace 2>/dev/null; kill $CFGPID $SKPID ${WMPID:-} 2>/dev/null
  '
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
G2=$(md5sum /etc/passwd | awk '{print $1}'); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"
echo ""
echo "=== 3-proc result (skmgr + Guppy/$GUPPY_C) ==="
echo "  skmgr: lines=$(sudo wc -l </tmp/sk.log 2>/dev/null) crash=$(sudo grep -acE "signal 6|signal 11|terminating signal" /tmp/sk.log 2>/dev/null) Q_SkMgr created=$(sudo grep -acE "Q_create .Q_SkMgr" /tmp/sk.log 2>/dev/null)"
echo "  skmgr serve activity (reads on 0x313/0x314): $(sudo grep -acE "Q_read.*0x31[34]|queue 0x31[34]" /tmp/sk.log 2>/dev/null)"
echo "  Guppy: lines=$(sudo wc -l </tmp/g_mmi.log 2>/dev/null) crash=$(sudo grep -acE "signal 6|signal 11|terminating signal" /tmp/g_mmi.log 2>/dev/null)"
echo "  Guppy SkMgr client: Q_ident Q_SkMgr=$(sudo grep -acE "Q_ident .Q_SkMgr" /tmp/g_mmi.log /tmp/g_cfg.log 2>/dev/null | paste -sd+ | bc 2>/dev/null || echo 0)"
echo "  softkey bind markers (Guppy): $(sudo grep -aoiE "softkey|Register|Binding.*failed|jh.softkey" /tmp/g_mmi.log /tmp/g_pystderr.log 2>/dev/null | sort | uniq -c | sort -rn | head -8 | tr '\n' ' ')"
echo "  X11 connect (Guppy): $(sudo grep -acE "X11-unix" /tmp/g_strace.log 2>/dev/null)  writev->X: $(sudo grep -acE "writev\(" /tmp/g_strace.log 2>/dev/null)"
echo "  Xvfb client windows: $(DISPLAY=$DISP xwininfo -root -children 2>/dev/null | grep -cE "0x[0-9a-f]+ \"")"
echo "  screenshot unique colours (1=blank): $(DISPLAY=$DISP convert /tmp/g_screen.xwd -format "%k" info: 2>/dev/null || echo n/a)"
echo "  --- Guppy python stderr (softkey?) ---"; sudo grep -aiE "softkey|binding|register" /tmp/g_pystderr.log /tmp/g_mmi.log 2>/dev/null | tail -15
echo "logs: /tmp/g_cfg.log /tmp/sk.log /tmp/g_mmi.log /tmp/g_strace.log /tmp/sk_strace.log"
