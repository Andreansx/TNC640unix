set -u
# Clean 2-proc ConfigServer + HrMmi, based on run_2proc_cfgfix (where ConfigServer reliably reaches its
# serve loop + serves IPO). The heavy run_hrmmi_fex.sh setup (dbus/heuserver + Xvfb + encfs staging)
# destabilizes ConfigServer's run-up; this strips it to the minimum so ConfigServer SERVES HrMmi's config.
# ConfigServer carries noopfree (survive HrMmi's 0x170501 over-free). Xvfb/openbox added so HrMmi can reach
# X after config. RELAY (HEROS_EVT_RELAY) DEFAULTS OFF now: the QEvtServer broadcasts are ConfigServer's
# EvtSendEvent TRACE stream (type 0x320221 "Message=... from thread=Nc", cfgserver.cpp:352) + a 4380B
# 0x40320461 + HrMmi's own subscribe echo — NONE of which are types HrMmi's HrModule::DispatchMessage
# handles, so forwarding them makes HrMmi fatally "Message was not handled". HrMmi's real config path is
# CfgConnectClient(0x1700c0, reply-to ".QueueHrMmi")->INJECT_ACK->config-data reply; set RELAY=QueueHrMmi
# to restore the old (crashing) relay for comparison.
REPO=/Users/andreansx/Documents/TNC640unix
CFG=$REPO/work/control/sysroot
R=/var/tmp/lr
CC=i686-linux-gnu-gcc
DISP=:99
CFGPRE="/lib/noopfree.so:/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"
# HrMmi also over-frees a corrupted-header chunk under FEX (same class as ConfigServer) while processing
# the relayed config -> guardfree skips the bad free so HrMmi survives + advances past Ev_receive(0x03011001).
MMIPRE="${MMI_FREEGUARD:-/lib/guardfree.so:}/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== build preloads ==="
$CC -shared -fPIC -O2 -o $R/lib/cfgfix.so $REPO/emulator/cfgfix.c -ldl || exit 1
$CC -shared -fPIC -O2 -Wl,--version-script=$REPO/emulator/arena.map -o $R/lib/arena_stub.so $REPO/emulator/arena_stub.c || exit 1
for s in herosapi_shim heros_rtos renamefix fexunmask noopfree guardfree; do $CC -shared -fPIC -O2 -o $R/lib/$s.so $REPO/emulator/$s.c -ldl || exit 1; done

# writable SYS mirror with resources (PLIB++ keymap) so HrMmi GUI init can load them after config
SYSW=/var/tmp/sysw; sudo rm -rf "$SYSW"; sudo mkdir -p "$SYSW"
sudo cp -aL "$CFG/config" "$SYSW/config"; sudo cp -aL "$CFG/batch" "$SYSW/batch"; sudo cp -aL "$CFG/resource" "$SYSW/resource" 2>/dev/null
sudo chmod -R a+rwX "$SYSW"
OEMW=/var/tmp/oemw; sudo rm -rf "$OEMW"; sudo cp -aL "$CFG/default/oem" "$OEMW" 2>/dev/null; sudo chmod -R a+rwX "$OEMW" 2>/dev/null
ln -sfn "$SYSW" /tmp/s; ln -sfn "$OEMW" /tmp/o; ln -sfn $R/heros5/bin /tmp/b

sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sudo pkill -x Xvfb 2>/dev/null; sudo pkill -x openbox 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm 2>/dev/null
# config-#6 prerequisites (cfgfix volume targets + productid cm=16)
sudo mkdir -p /mnt/sys/config /mnt/plc/config /mnt/sys/cache/nckern/productid /mnt/tnc/config
sudo cp -aL "$CFG/config/." /mnt/sys/config/ 2>/dev/null; sudo cp -aL "$CFG/config/." /mnt/tnc/config/ 2>/dev/null
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -aL "$CFG/default/oem/config/." /mnt/plc/config/ 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rwX /mnt/sys/config /mnt/plc/config /mnt/sys/cache /mnt/tnc 2>/dev/null
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0666 /dev/shm/_heusrv_shm'

if [ "${WITH_X:-1}" = 1 ]; then
  echo "=== start Xvfb $DISP + openbox ==="
  Xvfb $DISP -screen 0 1280x1024x16 -nolisten tcp >/tmp/x2.log 2>&1 & sleep 2
  DISPLAY=$DISP openbox >/tmp/ob2.log 2>&1 & sleep 1
fi

GUARD=$(md5sum /etc/passwd | awk '{print $1}')
sudo rm -f /tmp/c2_cfg.log /tmp/c2_mmi.log /tmp/c2_strace.log
sudo env R="$R" SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc OEME=/mnt/plc EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/mnt/sys/batch/heros5 \
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC: CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
  HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 \
  HEROSCALL_INJECT_ACK=1 HEROSCALL_INJECT_REREAD=1 HEROSCALL_INJECT_UPD=1 HEROS_EVENTS_PIPE=1 \
  HEROS_EVT_RELAY="${RELAY-}" HEROSCALL_DUMPQ="${DUMPQ:-0}" DISP="$DISP" CFGPRE="$CFGPRE" MMIPRE="$MMIPRE" \
  LANG=C LC_ALL=C \
  unshare -m bash -c '
    set -u; ulimit -c 0
    mount --make-rprivate /; mount --bind "$R/etc" /etc
    export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu DISPLAY=$DISP HEROSROOT=$R/heros5
    mkdir -p /etc/fonts; [ -e /etc/fonts/fonts.conf ] || printf "<?xml version=\"1.0\"?><fontconfig><dir>/usr/share/fonts</dir><cachedir>/tmp/fc</cachedir></fontconfig>" > /etc/fonts/fonts.conf
    cd /; ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"
    echo "### ConfigServer (bg, cfgfix + noopfree + relay) ###"
    ( env HEROS_FAKE_NS=1 HEROSCALL_VERBOSE=1 MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 \
        LD_PRELOAD="$CFGPRE" timeout -s KILL 200 FEXInterpreter "$R/heros5/bin/ConfigServer.elf" \
        -p=~/cfgserver cfgserver -f=/mnt/sys/config/jhconfigfiles.cfg -i=Nc > /tmp/c2_cfg.log 2>&1 ) &
    CFGPID=$!
    i=0; while [ $i -lt 200 ]; do
      grep -qE "HWS stub: replied|RUNUP_COMPLETE" /tmp/c2_cfg.log 2>/dev/null && { echo "  ConfigServer run-up at ${i}*0.5s"; break; }
      sleep 0.5; i=$((i+1)); done
    echo "  ConfigServer serve-loop reached: $(grep -acE "Ev_receive .*0101100" /tmp/c2_cfg.log 2>/dev/null)"
    sleep 8
    echo "### HrMmi.elf (fg, -k=NC, DISPLAY=$DISP) ###"
    ( sleep 150; rm -f /tmp/c2_screen.xwd; DISPLAY=$DISP xwd -root -out /tmp/c2_screen.xwd 2>/dev/null && echo "  screenshot bytes: $(wc -c </tmp/c2_screen.xwd 2>/dev/null)" ) &
    timeout -s KILL "${MMI_TIMEOUT:-180}" /usr/bin/strace -f -qq -e trace=openat,connect -o /tmp/c2_strace.log \
      env HEROSCALL_VERBOSE="${MMI_VERBOSE:-1}" MALLOC_ARENA_MAX=1 GLIBC_TUNABLES=glibc.malloc.arena_max=1 LD_PRELOAD="$MMIPRE" \
      FEXInterpreter "$R/heros5/bin/HrMmi.elf" -p=~/hrmmi hrmmi -k=NC > /tmp/c2_mmi.log 2>&1 || true
    echo "### HrMmi done ###"
    pkill -KILL -x strace 2>/dev/null; kill $CFGPID 2>/dev/null
  '
sudo pkill -KILL -x FEXInterpreter 2>/dev/null
G2=$(md5sum /etc/passwd | awk '{print $1}'); [ "$GUARD" = "$G2" ] && echo "GUARD OK" || echo "*** /etc CHANGED ***"
echo ""
echo "=== ConfigServer: served HrMmi? ==="
echo "  serve-loop: $(sudo grep -acE "Ev_receive .*0101100" /tmp/c2_cfg.log 2>/dev/null)  large 0x303 reads: $(sudo grep -aE "Q_read <- queue 0x303 size" /tmp/c2_cfg.log 2>/dev/null | awk '{if($NF>128)c++} END{print c+0}')  broadcasts->0x307: $(sudo grep -acE "Q_send -> queue 0x307" /tmp/c2_cfg.log 2>/dev/null)  crash: $(sudo grep -acE "free..: invalid|signal 6|ThrowException" /tmp/c2_cfg.log 2>/dev/null)"
echo "  EVT_RELAY: forwards=$(sudo grep -acE "EVT_RELAY: forwarding" /tmp/c2_cfg.log 2>/dev/null) flush=$(sudo grep -acE "EVT_RELAY: FLUSH" /tmp/c2_mmi.log 2>/dev/null)"
echo "=== HrMmi: progressed past Ev_receive(0x03011001)? reached X? ==="
echo "  0x30e reads: $(sudo grep -acE "Q_read <- queue 0x30e" /tmp/c2_mmi.log 2>/dev/null)  X11 connects: $(sudo grep -acE "X11-unix" /tmp/c2_strace.log 2>/dev/null)  crash: $(sudo grep -acE "signal 6|signal 11" /tmp/c2_mmi.log 2>/dev/null)"
echo "  HrMmi last RTOS:"; sudo grep -aE "^\[t[0-9]+ hc" /tmp/c2_mmi.log 2>/dev/null | tail -5
echo "logs: /tmp/c2_cfg.log /tmp/c2_mmi.log /tmp/c2_strace.log"
