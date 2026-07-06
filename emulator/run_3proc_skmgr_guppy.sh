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
# readfix.so (FIRST): retry the guest's read() on EINTR (emulator SIGUSR1 carrier) + transient
# EIO (vz disk under concurrent FEX I/O at startup). Prevents the winmgr/skmgr/Guppy "cold-VM"
# crash where a failed config/resource read throws IO::FileStream "File read error" -> the
# FThread EvtExceptionShell retry -> FModule eval-context myChildren[] OOB SIGSEGV (run-variance).
CFGPRE="/lib/readfix.so:/lib/noopfree.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
MMIPRE="/lib/readfix.so:${MMI_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
[ "${SKCONNFORCE:-0}" = "1" ] && MMIPRE="/lib/skconnforce.so:$MMIPRE"
SKPRE="/lib/readfix.so:${SK_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
GUPPY_BIN="${GUPPY_BIN:-Guppy.elf}"
GUPPY_C="${GUPPY_C:-HwSetup}"
GUPPY_ARGS="-R=UnloadHwSetup -v=c -C=$GUPPY_C -i=Nc -s=Sim"
SK_ARGS="${SK_ARGS:--w -k}"

echo "=== build preloads ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/nolimit.map -o $R/lib/nolimit.so $REPO/emulator/nolimit.c || exit 1
for s in herosapi_shim heros_rtos renamefix fexunmask noopfree guardfree readfix skspy skforce skconnforce wmstub gdactive; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c -ldl || exit 1; done

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
# ★ Back the Python OEM tree with tmpfs (RAM). The vz block backend (vda1) throws
# transient EIO under concurrent FEX I/O at constellation startup; Guppy's Python
# tokenizer reads HwViewer.py via glibc stdio getc-underflow (glibc-internal __read),
# which readfix's public read()/fread() interposer cannot retry -> "I/O error while
# reading" -> the OEM script never runs -> no softkey login. tmpfs reads hit RAM only,
# so they can never EIO. Populate at startup (low I/O), verifying the copy read cleanly.
sudo mountpoint -q /mnt/sys/Python && sudo umount -l /mnt/sys/Python 2>/dev/null
sudo mount -t tmpfs -o size=1g tmpfs /mnt/sys/Python
# Populate the tmpfs from a VM-LOCAL pre-staged mirror (PYTREE_LOCAL, default /var/tmp/pytree)
# when present — the virtiofs Mac-mount throws EDEADLK ("Resource deadlock avoided") / EIO on
# cp -aL (lseek sparse-probe), so the run must NOT read the Python tree from virtiofs at start.
# Prep once when idle:  tar -C $CFG/Python -cf - . | tar -C /var/tmp/pytree -xf -   (sequential
# read sidesteps the lseek deadlock; verify HwViewer.py md5). Fall back to a virtiofs tar-copy.
PYTREE_LOCAL="${PYTREE_LOCAL:-/var/tmp/pytree}"
if [ -f "$PYTREE_LOCAL/HwViewer/HwViewer.py" ]; then
  sudo cp -a "$PYTREE_LOCAL/." /mnt/sys/Python/ 2>/dev/null && echo "  Python tree staged to tmpfs from $PYTREE_LOCAL (local, reliable)"
else
  echo "  no local pytree — tar-copying from virtiofs (may EDEADLK)"
  sudo tar -C "$CFG/Python" -cf - . 2>/dev/null | sudo tar -C /mnt/sys/Python -xf - 2>/dev/null
fi
sudo md5sum /mnt/sys/Python/HwViewer/HwViewer.py >/dev/null 2>&1 && echo "  HwViewer.py present+readable" || echo "  *** HwViewer.py MISSING/UNREADABLE ***"
# ★ site-packages (the jh/pyjh Python binding Guppy imports) also on tmpfs — same vda1
# EIO problem: import jh reads jh-X.Y/*.py from /mnt/sys/usr/lib/python/site-packages,
# which EIO'd under load -> "JH library not available" -> NameError: 'jh' is not defined.
SPLOCAL="${SPLOCAL:-/var/tmp/pytree-sp}"
sudo mkdir -p /mnt/sys/usr/lib/python/site-packages
sudo mountpoint -q /mnt/sys/usr/lib/python/site-packages && sudo umount -l /mnt/sys/usr/lib/python/site-packages 2>/dev/null
sudo mount -t tmpfs -o size=256m tmpfs /mnt/sys/usr/lib/python/site-packages
if [ -f "$SPLOCAL/pyjh.py" ]; then
  sudo cp -a "$SPLOCAL/." /mnt/sys/usr/lib/python/site-packages/ 2>/dev/null && echo "  site-packages staged to tmpfs from $SPLOCAL"
else
  sudo tar -C "$CFG/usr/lib/python/site-packages" -cf - . 2>/dev/null | sudo tar -C /mnt/sys/usr/lib/python/site-packages -xf - 2>/dev/null
fi
sudo md5sum /mnt/sys/usr/lib/python/site-packages/pyjh.py >/dev/null 2>&1 && echo "  pyjh.py present+readable" || echo "  *** pyjh.py MISSING/UNREADABLE ***"
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
  HEROS_CFG_REPLY_ROUTE=1 HEROS_EV_SIGWAKE="${EV_SIGWAKE:-0}" HEROSCALL_PIDENT_SELF="${PIDENT_SELF:-1}" HEROSCALL_SK_REPLY_FORCE="${SK_REPLY_FORCE:-1}" HEROSCALL_DUMPQ="${DUMPQ:-0}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_CAPTURE_TYPE="${CAPTURE_TYPE:-}" HEROSCALL_INJECT_SK_FLOW="${INJECT_SK_FLOW:-0}" HEROSCALL_INJECT_AREA_ACK="${INJECT_AREA_ACK:-0}" HEROSCALL_INJECT_WMGR_ACK="${INJECT_WMGR_ACK:-0}" HEROSCALL_WMGR_SCREEN="${WMGR_SCREEN:-0}" HEROSCALL_WMGR_MSGDUMP="${WMGR_MSGDUMP:-0}" DISP="$DISP" \
  HEROSCALL_INJECT_SK_ACTIVATE="${INJECT_SK_ACTIVATE:-0}" HEROSCALL_SK_ACT_THRESH="${SK_ACT_THRESH:-5}" HEROSCALL_SK_ACT_SCREEN="${SK_ACT_SCREEN:-}" HEROSCALL_SK_ACT_GROUP="${SK_ACT_GROUP:-0}" HEROSCALL_SK_ACT_HANDLE="${SK_ACT_HANDLE:-13}" HEROSCALL_SK_ACT_BOOL="${SK_ACT_BOOL:-0}" \
  HEROSCALL_AREA_RECT_FORCE="${AREA_RECT_FORCE:-0}" \
  HEROSCALL_INJECT_WMGR_ACTIVATE="${INJECT_WMGR_ACTIVATE:-0}" HEROSCALL_WMACT_DELAY="${WMACT_DELAY:-20}" HEROSCALL_WMACT_SCREEN="${WMACT_SCREEN:-1}" HEROSCALL_WMACT_SELECT="${WMACT_SELECT:-0}" HEROSCALL_WM_WIRE_DIR="${WM_WIRE_DIR:-/tmp}" \
  HEROSCALL_EV_TRACE_BIT="${EV_TRACE_BIT:-0}" HEROSCALL_EV_TRACE_TASK="${EV_TRACE_TASK:-0}" HEROSCALL_EV_TRACE_BUDGET="${EV_TRACE_BUDGET:-24}" HEROSCALL_EV_TRACE_EXACT="${EV_TRACE_EXACT:-0}" \
  HEROSCALL_EV_INJECT_WANT="${EV_INJECT_WANT:-0}" HEROSCALL_EV_INJECT_BIT="${EV_INJECT_BIT:-0}" HEROSCALL_EV_UNBLOCK_MS="${EV_UNBLOCK_MS:-0}" \
  G_EVW="${G_EVW:-0}" G_EVB="${G_EVB:-0}" G_EVMS="${G_EVMS:-0}" SK_EVW="${SK_EVW:-0}" SK_EVB="${SK_EVB:-0}" SK_EVMS="${SK_EVMS:-0}" \
  HEROSCALL_EV_FORCE_TASK="${EV_FORCE_TASK:-0}" HEROSCALL_EV_FORCE_BIT="${EV_FORCE_BIT:-0}" \
  CFGPRE="$CFGPRE" MMIPRE="$MMIPRE" SKPRE="$SKPRE" USE_XVFB="$USE_XVFB" NO_NCK_WINMGR="${NO_NCK_WINMGR:-}" WMFORCE="${WMFORCE:-}" SKFORCE="${SKFORCE:-}" \
  HWVIEWER_SK_USAGE="${HWVIEWER_SK_USAGE:-}" HWVIEWER_GEOMETRY="${HWVIEWER_GEOMETRY:-}" WINMGR="${WINMGR:-0}" WM_LAYOUT="${WM_LAYOUT:-}" WM_SIZE="${WM_SIZE:-}" WM_VERBOSE="${WM_VERBOSE:-1}" WM_SEGVBT="${WM_SEGVBT:-0}" WM_STRACE_TRACE="${WM_STRACE_TRACE:-connect,writev}" WM_SEM_INIT="${WM_SEM_INIT:-0}" WM_SEM_FORCE_OK="${WM_SEM_FORCE_OK:-4000}" WM_PNAME="${WM_PNAME:-1}" SYSFIRE="${SYSFIRE:-0}" SYSFIRE_MASK="${SYSFIRE_MASK:-00ff0000}" WM_FIRE_LIMIT="${WM_FIRE_LIMIT:-0}" WM_FIRE_YIELD="${WM_FIRE_YIELD:-0}" \
  GUPPY_BIN="$GUPPY_BIN" GUPPY_ARGS="$GUPPY_ARGS" GUPPY_C="$GUPPY_C" SK_ARGS="$SK_ARGS" GUPPY_DISPLAY="${GUPPY_DISPLAY:-}" HWV_FORCE_FS="${HWV_FORCE_FS:-}" SHOT_AT="${SHOT_AT:-150}" WININSPECT="${WININSPECT:-0}" MAPFORCE="${MAPFORCE:-}" WMPOKE="${WMPOKE:-}" WMSHOW="${WMSHOW:-}" WMFLOAT="${WMFLOAT:-}" BARCOPY="${BARCOPY:-}" BARCOPY_PER="${BARCOPY_PER:-150}" LANG=C LC_ALL=C \
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
      # ★ winmgr render-crossing config (2026-07-05): SEM_INIT=0 + SEM_FORCE_OK makes the
      # AppStartMaster start-ack sems (winmgrC/winmgrI) BLOCK then force -> winmgr runs
      # WmModule::Initialize -> creates+maps its screen-layout windows (Machine/Edit); PNAME=1
      # fixes the P_name(-1) garbage sub-thread SIGSEGV. (readfix in $SKPRE fixes the EIO crash.)
      # WM_SEGVBT diagnostic preload: 1 = throwcatch + segvbt (full), 2 = segvbt ONLY
      # (minimal perturbation — the throwcatch __cxa_throw interposer can shift winmgr timing
      # enough to hide the run-variant crash; segvbt alone captures the fault EIP/regs).
      case "${WM_SEGVBT:-0}" in 1) WMSEG=/lib/throwcatch.so:/lib/segvbt.so: ;; 2) WMSEG=/lib/segvbt.so: ;; *) WMSEG= ;; esac
      ( env HEROSCALL_VERBOSE="${WM_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_SEM_INIT="${WM_SEM_INIT:-0}" HEROSCALL_SEM_FORCE_OK="${WM_SEM_FORCE_OK:-4000}" HEROSCALL_PNAME="${WM_PNAME:-1}" HEROSCALL_SYSEVENT_AUTOFIRE="${SYSFIRE:-0}" HEROSCALL_SYSEVENT_FIRE_MASK="${SYSFIRE_MASK:-00ff0000}" HEROSCALL_SYSEVENT_FIRE_LIMIT="${WM_FIRE_LIMIT:-0}" HEROSCALL_SYSEVENT_FIRE_YIELD_US="${WM_FIRE_YIELD:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
          LD_PRELOAD="${WMSEG}$SKPRE" timeout -s KILL 300 /usr/bin/strace -f -qq -e trace=${WM_STRACE_TRACE:-connect,writev} -o /tmp/wm_strace.log \
          FEXInterpreter "$R/heros5/bin/winmgr.elf" -p=~/winmgr winmgr \
          -m=5 -i=$WM_LAYOUT -o=afk -s=$WM_SIZE \
          -k=%SYS%/resource/keymap_us101.xml -c=%SYS%/resource/charmap_us101.xml -f=%SYS%/resource/functionkeymap_us101.xml \
          > /tmp/wm.log 2>&1 ) &
      WMPID=$!
      i=0; while [ $i -lt 160 ]; do
        grep -qaE "Q_create .Q_WMGR" /tmp/wm.log 2>/dev/null && { echo "  winmgr created Q_WMGR at ${i}*0.5s"; break; }
        sleep 0.5; i=$((i+1)); done
      # ★ winmgr HEAD-START knob (2026-07-06, default OFF): winmgr creates its screen-layout
      # windows (~127 X writev) then a render-thread sub-thread SIGSEGVs (a DIRECT deref, NOT a
      # catchable C++ throw — throwcatch caught 0; libheros_sigfaterr recovers non-fatally but
      # winmgr WEDGES + its X connection drops -> the Machine/Edit screen windows are DESTROYED
      # -> the Guppy OEM window never realizes -> no softkey login. Tested: the crash reproduces
      # at ~127 writev whether or not skmgr/Guppy are up yet, so a head-start does NOT avoid it;
      # this knob (wait for winmgr writev>=N before launching skmgr/Guppy) is left for further
      # experiments on the documented winmgr render-thread frontier. WM_HEADSTART_WRITEV=0 -> old 6s.
      HSW="${WM_HEADSTART_WRITEV:-0}"
      if [ "$HSW" -gt 0 ] 2>/dev/null; then
        j=0; while [ $j -lt "${WM_HEADSTART_MAX:-45}" ]; do
          wv=$(grep -ac writev /tmp/wm_strace.log 2>/dev/null || echo 0)
          [ "${wv:-0}" -ge "$HSW" ] && { echo "  winmgr screens created (writev=$wv) — head-start done at ${j}s"; break; }
          grep -qaE "terminating signal 11" /tmp/wm.log 2>/dev/null && { echo "  winmgr SIGSEGV during head-start (writev=$wv)"; break; }
          sleep 1; j=$((j+1)); done
        sleep 3
      else
        sleep 6
      fi
    fi

    echo "### skmgr (bg, -p=~/skmgr skmgr $SK_ARGS) ###"
    ( env HEROSCALL_VERBOSE="${SK_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" HEROSCALL_EMPTYPOLL_DIAG="${EMPTYPOLL_DIAG:-0}" HEROSCALL_EMPTYPOLL_YIELD="${EMPTYPOLL_YIELD:-0}" HEROSCALL_WMGR_SCREEN="${WMGR_SCREEN:-0}" HEROSCALL_WMQ_BREAK="${WMQ_BREAK:-0}" HEROSCALL_WMQ_BREAK_N="${WMQ_BREAK_N:-2000}" HEROSCALL_SELECT_CAP_MS="${SELECT_CAP_MS:-0}" HEROSCALL_PPOLL_DBG="${PPOLL_DBG:-0}" HEROSCALL_SYSEVENT_AUTOFIRE="${SK_SYSFIRE:-0}" HEROSCALL_SYSEVENT_FIRE_MASK="${SK_SYSFIRE_MASK:-00010000}" HEROSCALL_EV_TIMEOUT_CAP_MS="${SK_EVCAP:-0}" HEROSCALL_EV_INJECT_WANT="${SK_EVW:-0}" HEROSCALL_EV_INJECT_BIT="${SK_EVB:-0}" HEROSCALL_EV_UNBLOCK_MS="${SK_EVMS:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
        LD_PRELOAD="$SKPRE" timeout -s KILL 300 /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/sk_strace.log \
        FEXInterpreter "$R/heros5/bin/skmgr.elf" -p=~/skmgr skmgr $SK_ARGS > /tmp/sk.log 2>&1 ) &
    SKPID=$!
    i=0; while [ $i -lt 120 ]; do
      grep -qE "Q_create .Q_SkMgrCtrl" /tmp/sk.log 2>/dev/null && { echo "  skmgr created Q_SkMgr/Q_SkMgrCtrl at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    sleep "${GUPPY_DELAY:-6}"

    if [ -n "${WMPOKE:-}" ] && [ -x /tmp/wmpoke ]; then
      echo "### wmpoke desktop ${WMPOKE} (bg, drives winmgr CheckRootPropChange->OnDesktopChanged->Activate) ###"
      ( DISPLAY=$DISP /tmp/wmpoke ${WMPOKE} "${MMI_TIMEOUT:-200}" 800 > /tmp/wmpoke.log 2>&1 ) &
    fi
    if [ -n "${WMSHOW:-}" ] && [ -x /tmp/wmshow ]; then
      echo "### wmshow (bg, winmgr screen-show stand-in: maps winmgr+skmgr subtree continuously) ###"
      ( DISPLAY=$DISP /tmp/wmshow "${MMI_TIMEOUT:-200}" 250 > /tmp/wmshow.log 2>&1 ) &
    fi
    if [ -n "${WMFLOAT:-}" ] && [ -x /tmp/wmfloat ]; then
      echo "### wmfloat (bg, reparent skmgr softkey strip to root@0,936 + map subtree, continuously) ###"
      ( DISPLAY=$DISP /tmp/wmfloat "${MMI_TIMEOUT:-200}" "${WMFLOAT_PER:-200}" > /tmp/wmfloat.log 2>&1 ) &
    fi
    if [ -n "${BARCOPY:-}" ] && [ -x /tmp/barcopy ]; then
      echo "### barcopy (bg, copy skmgr BAR PIXMAP to visible override-redirect window at 0,936) ###"
      ( DISPLAY=$DISP /tmp/barcopy "${MMI_TIMEOUT:-200}" "${BARCOPY_PER:-150}" > /tmp/barcopy.log 2>&1 ) &
    fi

    for p in pystdout pystderr ncstdout ncstderr; do
      rm -f /tmp/__helogpipe_$p; mkfifo /tmp/__helogpipe_$p 2>/dev/null
      ( timeout "${MMI_TIMEOUT:-200}" cat /tmp/__helogpipe_$p > /tmp/g_$p.log 2>/dev/null & )
    done

    GDISP="${GUPPY_DISPLAY:-$DISP}"   # faithful bind: gtk_external_display_active() (libgtkbind 0x16510) returns FALSE only when gdk_get_display()==":0.0" EXACTLY; then CreateWindowData runs the WndFullScreen dispatch (sets window-record +0x1C) so jh.softkey.Register binds -> login q_sends to Q_SkMgr -> skmgr serves. A plain ":0" gives gdk_get_display()==":0" -> external active -> dispatch skipped -> "Binding softkey resource to window failed". So promote :0 -> :0.0 for Guppy (Xvfb still on :0). NO binary patch, proper window geometry.
    case "$GDISP" in :0) GDISP=:0.0;; esac
    echo "### Guppy ($GUPPY_BIN $GUPPY_ARGS, DISPLAY=$GDISP) ###"
    [ "$USE_XVFB" = "1" ] && ( sleep "${SHOT_AT:-150}"; rm -f /tmp/g_screen.xwd /tmp/skwin_*.png /tmp/skwin_info.txt; DISPLAY=$DISP xwininfo -root -tree > /tmp/g_windows.txt 2>&1;
      if [ -n "${MAPFORCE:-}" ] && [ -x /tmp/mapwin ]; then ids=""; for nm in $MAPFORCE; do case "$nm" in 0x*) id="$nm";; *) id=$(grep -E "\"$nm\"" /tmp/g_windows.txt | grep -oE "0x[0-9a-f]+" | head -1);; esac; [ -n "$id" ] && ids="$ids $id"; done; echo "  MAPFORCE [$MAPFORCE] -> [$ids]"; [ -n "$ids" ] && DISPLAY=$DISP /tmp/mapwin $ids 2>&1 | sed "s/^/  /"; sleep 2; DISPLAY=$DISP xwininfo -root -tree > /tmp/g_windows.txt 2>&1; fi;
      DISPLAY=$DISP xwd -root -out /tmp/g_screen.xwd 2>/dev/null && echo "  screenshot bytes: $(wc -c </tmp/g_screen.xwd 2>/dev/null)";
      if [ "${WININSPECT:-0}" = "1" ]; then for wid in $(grep -E "\+0\+936|HwViewer|Machine|Edit|0x80001f|0x4000[0-9a-f][0-9a-f]|0x2001c0|Screen" /tmp/g_windows.txt | grep -oE "0x[0-9a-f]+" | sort -u); do echo "=== $wid ===" >> /tmp/skwin_info.txt; DISPLAY=$DISP xwininfo -id $wid -stats 2>&1 | grep -E "Window id|Map State|Width:|Height:|Corners|Override" >> /tmp/skwin_info.txt 2>&1; DISPLAY=$DISP xwd -id $wid -out /tmp/skwin_$wid.xwd 2>/dev/null && DISPLAY=$DISP convert /tmp/skwin_$wid.xwd /tmp/skwin_$wid.png 2>/dev/null && rm -f /tmp/skwin_$wid.xwd; done; fi ) &
    timeout -s KILL "${MMI_TIMEOUT:-200}" /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/g_strace.log \
      env DISPLAY="$GDISP" HEROSCALL_VERBOSE="${MMI_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 PYTHONHOME=/usr NO_NCK_WINMGR="$NO_NCK_WINMGR" WMFORCE="$WMFORCE" SKFORCE="$SKFORCE" HWVIEWER_SK_USAGE="$HWVIEWER_SK_USAGE" HWVIEWER_GEOMETRY="$HWVIEWER_GEOMETRY" HEROSCALL_WMQ_BREAK="${WMQ_BREAK:-0}" HEROSCALL_WMQ_BREAK_N="${WMQ_BREAK_N:-2000}" HEROSCALL_EMPTYPOLL_YIELD="${EMPTYPOLL_YIELD:-0}" HEROSCALL_WMGR_ROOT="${WMGR_ROOT:-0}" HEROSCALL_WMGR_SCREEN="${WMGR_SCREEN:-0}" HWV_FORCE_FS="${HWV_FORCE_FS:-}" HEROSCALL_EV_NOWAIT_YIELD="${EV_NOWAIT_YIELD:-0}" HEROSCALL_EV_NOWAIT_YIELD_N="${EV_NOWAIT_YIELD_N:-64}" HEROSCALL_EV_INJECT_WANT="${G_EVW:-0}" HEROSCALL_EV_INJECT_BIT="${G_EVB:-0}" HEROSCALL_EV_UNBLOCK_MS="${G_EVMS:-0}" LD_PRELOAD="$MMIPRE" \
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
