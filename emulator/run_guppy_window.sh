set -u
# Guppy WINDOW render — drive Guppy.elf as the OEM/Python GTK runtime to LOAD A REAL SCRIPT and
# create its WndFullScreen GTK window FEX-native on ARM64.
#
# KEY CORRECTION (2026-06-26): Guppy.elf is NOT the main operator MMI screen — it is the OEM/Python
# *script-runtime launcher*. Each Guppy invocation runs a DIFFERENT GTK Python script selected by
# the `-C=<key>` command-line option (GuppyOemThread reads option 67='C' -> configurationKey ->
# GuppyOemModule::GetConfiguration does CfgMailslotQueue::GetData(CfgOemScript/CfgPythonScript,
# key) -> script path -> GuppyOemModule::Execute -> PyJHKernel::Execute(script)). The batch default
# `-R=UnloadOEM` (no -C) is the OEM custom screen, which a demo station has NONE of -> the
# "terminate: no script" we saw is CORRECT/expected, not a gate. The real operator screen (Manual
# operation / Programming) is machoper.elf / Fred.elf / simulo.elf (processName ~/mmi), a separate
# target. THIS run proves the FEX-native GTK2+Python2.7+X11 render path end-to-end by giving Guppy a
# script that EXISTS: -C=HwSetup -> jh.cfg CfgOemScript key:"HwSetup" path:"SYS:/Python/HwViewer/
# HwViewer.py HwSetup" -> a real GTK fullscreen commissioning window.
#
# Knobs: GUPPY_C=HwSetup|HwViewer|SParDialog (configurationKey); MMI_TIMEOUT=220; MMI_VERBOSE=1;
#        DUMPQ=1; HSTRACE=1; STAGE=1 (re-stage the Python runtime; auto-skips if already staged).
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
TGT=$REPO/work/target/rootfs
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
# XDISPLAY=localhost:0 (over a reverse SSH tunnel to the Mac's XQuartz) renders the window as a
# NATIVE macOS window (Phase B, no VNC); default :99 = local Xvfb framebuffer (for screenshots).
DISP="${XDISPLAY:-:99}"
USE_XVFB=1; case "$DISP" in :99) USE_XVFB=1;; *) USE_XVFB=0;; esac
CFGPRE="/lib/noopfree.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
MMIPRE="${MMI_FREEGUARD:-/lib/guardfree.so:}/lib/nolimit.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
GUPPY_BIN="${GUPPY_BIN:-Guppy.elf}"
GUPPY_C="${GUPPY_C:-HwSetup}"
GUPPY_ARGS="-R=UnloadHwSetup -v=c -C=$GUPPY_C -i=Nc -s=Sim"

echo "=== build preloads ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/nolimit.map -o $R/lib/nolimit.so $REPO/emulator/nolimit.c || exit 1
for s in herosapi_shim heros_rtos renamefix fexunmask noopfree guardfree; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c -ldl || exit 1; done

echo "=== stage Python 2.7 + pygtk + pyjh runtime into the FEX rootfs (idempotent) ==="
if [ "${STAGE:-0}" = "1" ] || [ ! -d "$R/usr/lib/python2.7" ]; then
  sudo mkdir -p $R/usr/lib $R/usr/bin
  sudo cp -a $TGT/usr/lib/python2.7 $R/usr/lib/python2.7
  sudo cp -a $TGT/usr/bin/python2.7 $R/usr/bin/python2.7 2>/dev/null
  sudo mkdir -p $R/usr/lib/python/site-packages
  sudo cp -a $CFG/usr/lib/python/site-packages/. $R/usr/lib/python/site-packages/
  # gdk-pixbuf image loaders (.so modules + loaders.cache) — dlopened at runtime, not in the NEEDED
  # closure; without them gdk-pixbuf cannot decode the GTK UI bitmaps ("Couldn't recognize image format")
  sudo cp -a $TGT/usr/lib/gdk-pixbuf-2.0 $R/usr/lib/gdk-pixbuf-2.0
  echo "  staged python2.7=$(du -sh $R/usr/lib/python2.7|cut -f1) jh-3.0=$(ls -d $R/usr/lib/python/site-packages/jh-3.0 2>/dev/null) pixbuf-loaders=$(ls $R/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders/*.so 2>/dev/null|wc -l)"
else
  echo "  already staged ($(du -sh $R/usr/lib/python2.7|cut -f1)); STAGE=1 to refresh"
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
sudo mkdir -p /mnt/plc/service && sudo chmod -R a+rwX /mnt/plc/service   # GuppyOemModule traceback log OEM:/service/<job>.a
# stage the Python script tree + pyjh under SYS: (= /mnt/sys) for FSystemPathname resolution
sudo mkdir -p /mnt/sys/Python /mnt/sys/usr/lib/python
sudo cp -aL "$CFG/Python/." /mnt/sys/Python/ 2>/dev/null
sudo cp -aL "$CFG/usr/lib/python/site-packages" /mnt/sys/usr/lib/python/site-packages 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rwX /mnt/sys/config /mnt/plc/config /mnt/sys/cache /mnt/tnc /mnt/sys/Python /mnt/sys/usr 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'

if [ "$USE_XVFB" = "1" ]; then
  echo "=== start Xvfb $DISP + openbox ==="
  Xvfb $DISP -screen 0 1280x1024x16 -nolisten tcp >/tmp/x2.log 2>&1 & sleep 2
  DISPLAY=$DISP openbox >/tmp/ob2.log 2>&1 & sleep 1
else
  echo "=== using EXTERNAL X server DISPLAY=$DISP (e.g. Mac XQuartz over reverse tunnel) — no Xvfb ==="
fi

GUARD=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/g_cfg.log /tmp/g_mmi.log /tmp/g_strace.log
sudo env R="$R" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_EVT_ACK=1 HEROSCALL_INJECT_PEER_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 HEROS_EVENTS_PIPE=1 \
  HEROS_CFG_REPLY_ROUTE=1 HEROSCALL_DUMPQ="${DUMPQ:-0}" HEROSCALL_HSTRACE="${HSTRACE:-0}" DISP="$DISP" CFGPRE="$CFGPRE" MMIPRE="$MMIPRE" \
  GUPPY_BIN="$GUPPY_BIN" GUPPY_ARGS="$GUPPY_ARGS" GUPPY_C="$GUPPY_C" LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /; mount --bind "$R/etc" /etc
    # the rootfs /etc (bound over /etc) lacks /etc/hosts → resolving the X DISPLAY host (localhost) does a
    # DNS lookup to 127.0.0.1:53 (no server) and STALLS the X connect; give it a minimal hosts file.
    grep -q "127.0.0.1" /etc/hosts 2>/dev/null || printf "127.0.0.1\tlocalhost\n::1\tlocalhost\n" > /etc/hosts
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu DISPLAY=$DISP HEROSROOT=$R/heros5
    export PYTHONHOME=/usr
    mkdir -p /etc/fonts; [ -e /etc/fonts/fonts.conf ] || printf "<?xml version=\"1.0\"?><fontconfig><dir>/usr/share/fonts</dir><cachedir>/tmp/fc</cachedir></fontconfig>" > /etc/fonts/fonts.conf
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"
    echo "### ConfigServer (bg) ###"
    ( env HEROS_FAKE_NS=1 HEROSCALL_VERBOSE=1 MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
        LD_PRELOAD="$CFGPRE" timeout -s KILL 280 FEXInterpreter "$R/heros5/bin/ConfigServer.elf" \
        -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/g_cfg.log 2>&1 ) &
    CFGPID=$!
    i=0; while [ $i -lt 200 ]; do
      grep -qE "HWS stub: replied|RUNUP_COMPLETE" /tmp/g_cfg.log 2>/dev/null && { echo "  ConfigServer run-up at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    sleep 8
    # HeLogger stdout/stderr FIFOs: libheros sys_redirect_log redirects Python/NC stdio here; in the
    # full constellation a central HeLogger creates+reads them. Create them + background readers so the
    # writer side (Guppy) opens cleanly and we capture Python stdout/stderr/tracebacks.
    for p in pystdout pystderr ncstdout ncstderr; do
      rm -f /tmp/__helogpipe_$p; mkfifo /tmp/__helogpipe_$p 2>/dev/null
      ( timeout "${MMI_TIMEOUT:-200}" cat /tmp/__helogpipe_$p > /tmp/g_$p.log 2>/dev/null & )
    done
    echo "### Guppy ($GUPPY_BIN $GUPPY_ARGS, DISPLAY=$DISP) ###"
    [ "$USE_XVFB" = "1" ] && ( sleep "${SHOT_AT:-150}"; rm -f /tmp/g_screen.xwd; DISPLAY=$DISP xwd -root -out /tmp/g_screen.xwd 2>/dev/null && echo "  screenshot bytes: $(wc -c </tmp/g_screen.xwd 2>/dev/null)" ) &
    timeout -s KILL "${MMI_TIMEOUT:-200}" /usr/bin/strace -f -qq -e trace=openat,connect,writev -o /tmp/g_strace.log \
      env HEROSCALL_VERBOSE="${MMI_VERBOSE:-1}" HEROSCALL_HSTRACE="${HSTRACE:-0}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 PYTHONHOME=/usr LD_PRELOAD="$MMIPRE" \
      FEXInterpreter "$R/heros5/bin/$GUPPY_BIN" -p=~/Guppy Guppy $GUPPY_ARGS > /tmp/g_mmi.log 2>&1 || true
    echo "### Guppy done ###"
    pkill -KILL -x strace 2>/dev/null; kill $CFGPID 2>/dev/null
  '
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
G2=$(md5sum /etc/passwd | awk '{print $1}'); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"
echo ""
echo "=== Guppy window result (configurationKey=$GUPPY_C) ==="
echo "  ConfigServer serve-loop: $(sudo grep -acE "Ev_receive .*0101100" /tmp/g_cfg.log 2>/dev/null)  CfgOemScript served: $(sudo grep -acE "CfgOemScript|HwSetup|HwViewer" /tmp/g_cfg.log 2>/dev/null)"
echo "  Guppy lines: $(sudo wc -l </tmp/g_mmi.log 2>/dev/null)  crash(sig6/11): $(sudo grep -acE "signal 6|signal 11|terminating signal" /tmp/g_mmi.log 2>/dev/null)"
echo "  PYTHON markers: $(sudo grep -acE "PYTHON|Traceback|ImportError|terminate: no script" /tmp/g_mmi.log 2>/dev/null)"
echo "  script-name resolved (Execute): $(sudo grep -aoE "HwViewer.py|SParDialog.py|Client.py|no script" /tmp/g_mmi.log 2>/dev/null | sort -u | tr '\n' ' ')"
echo "  X11 connect: $(sudo grep -acE "X11-unix" /tmp/g_strace.log 2>/dev/null)  writev->X (drawing): $(sudo grep -acE "writev\(" /tmp/g_strace.log 2>/dev/null)"
echo "  GTK/window markers: $(sudo grep -acE "[Gg]tk|XCreateWindow|gdk|show_all|window shown" /tmp/g_mmi.log 2>/dev/null)"
echo "  Xvfb client windows: $(DISPLAY=$DISP xwininfo -root -children 2>/dev/null | grep -cE "0x[0-9a-f]+ \"")"
echo "  screenshot unique colours (1=blank): $(DISPLAY=$DISP convert /tmp/g_screen.xwd -format "%k" info: 2>/dev/null || echo n/a)"
echo "  --- Python stdout (helogpipe) ---"; sudo tail -25 /tmp/g_pystdout.log 2>/dev/null
echo "  --- Python stderr (helogpipe) ---"; sudo tail -25 /tmp/g_pystderr.log 2>/dev/null
echo "  --- traceback log /mnt/plc/service/$GUPPY_C.a ---"; sudo tail -15 /mnt/plc/service/$GUPPY_C.a 2>/dev/null
echo "  Guppy stderr/python tail:"; sudo grep -avE "^\[t[0-9]+ hc|^\[rtos\]|BUS_ADRALN|unaligned atomic" /tmp/g_mmi.log 2>/dev/null | tail -20
echo "logs: /tmp/g_cfg.log /tmp/g_mmi.log /tmp/g_strace.log /tmp/g_pystdout.log /tmp/g_pystderr.log  screenshot: /tmp/g_screen.xwd"
