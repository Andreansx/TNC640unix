#!/bin/bash
# run_appstart_fex.sh — AppStartMP (the HeROS process-manager / constellation launcher, S85) under FEX,
# against the now-running 3-server foundation (dbus + auth-daemon + heuserver) + Xvfb + openbox.
#
# GOAL (scouting): the prior qemu-i386 run got AppStartMP past "waiting for X-Server" + "waiting for
# X-WindowManager" to the constellation-spawn stage, where its forked `heuseradmin` child failed
# "Cannot connect to stream socket: Connection refused" — because heuserver was NOT up. heuserver now
# BINDS 127.0.0.1:19093 under FEX, so this run tests whether heuseradmin CONNECTS, and maps the next
# concrete constellation blocker. Beyond that is the documented full-system / Qt-MMI ceiling.
#
# Containment: everything (3 servers + AppStartMP) runs in ONE mount-ns with rootfs /etc bound over
# /etc (FEX /etc-leak guard; heuserver-as-root corruption protection). Xvfb/openbox are NATIVE aarch64,
# started OUTSIDE the ns; their X socket in the shared /tmp is reachable inside. Run in lima `tnc`.
set -u
R=/var/tmp/lr
EMU="$(cd "$(dirname "$0")" && pwd)"
CFG=/Users/andreansx/Documents/TNC640unix/work/control/sysroot
CC=i686-linux-gnu-gcc
FEXLIBS=/usr/lib/aarch64-linux-gnu:/lib/aarch64-linux-gnu
DISP=:99

echo "=== [0] sanity ==="
which FEXInterpreter Xvfb openbox >/dev/null || { echo "FATAL: missing FEXInterpreter/Xvfb/openbox"; exit 1; }
[ -e "$R/heros5/bin/AppStartMP.elf" ] || { echo "FATAL: AppStartMP.elf missing"; exit 1; }
[ -e "$CFG/batch/TNC640heros.txt" ] || { echo "FATAL: TNC640heros.txt missing"; exit 1; }

echo "=== [1] rebuild preloads + AppStartMP closure ==="
for s in arena_stub; do $CC -shared -fPIC -O2 -Wl,--version-script="$EMU/arena.map" -o "$R/lib/$s.so" "$EMU/$s.c" 2>&1 | sed "s/^/  $s: /"; done
for s in herosapi_shim renamefix fexunmask heros_rtos; do $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$EMU/$s.c" 2>&1 | sed "s/^/  $s: /"; done
$CC -shared -fPIC -O2 -o "$R/lib/openlog.so" "$EMU/openlog.c" -ldl 2>&1 | sed "s/^/  openlog: /"
$CC -shared -fPIC -O2 -o "$R/lib/cfgfix.so" "$EMU/cfgfix.c" -ldl 2>&1 | sed "s/^/  cfgfix: /"   # config-#6 fix
# Copy AppStartMP's i386 closure into the rootfs (cp -aL; overlaps the IPO/ConfigServer closure mostly).
sudo bash -c '
SRC=/Users/andreansx/Documents/TNC640unix/work/target/rootfs; R='"$R"'
declare -A S; SKIP="libc.so.6 libpthread.so.0 librt.so.1 libdl.so.2 libm.so.6 ld-linux.so.2 libresolv.so.2 libutil.so.1 libnsl.so.1"
fl(){ find "$SRC/heros5/bin" "$SRC/usr/lib" "$SRC/lib" -name "$1" 2>/dev/null|head -1; }
cc(){ local l="$1"; [ -n "${S[$l]:-}" ]&&return; S[$l]=1; case " $SKIP " in *" $l "*)return;;esac
  local p; p=$(fl "$l"); [ -z "$p" ]&&return; local rel=${p#$SRC/}
  if ! { [ -e "$R/$rel" ]&&file -L "$R/$rel" 2>/dev/null|grep -q "ELF 32-bit"; }; then mkdir -p "$R/$(dirname "$rel")"; rm -f "$R/$rel"; cp -aL "$p" "$R/$rel"; fi
  for n in $(i686-linux-gnu-objdump -p "$p" 2>/dev/null|awk "/NEEDED/{print \$2}"); do cc "$n"; done; }
for n in $(i686-linux-gnu-objdump -p "$R/heros5/bin/AppStartMP.elf" 2>/dev/null|awk "/NEEDED/{print \$2}"); do cc "$n"; done
echo "  AppStartMP closure ensured (${#S[@]} nodes)"'

echo "=== [2] FEX config + writable SYS (AppStartMP writes SYS\\runtime) + clean shm ==="
sudo mkdir -p /root/.fex-emu; printf '{"Config":{"RootFS":"%s"}}\n' "$R" | sudo tee /root/.fex-emu/Config.json >/dev/null
# Writable SYS mirror: real config/batch (RO source) + a writable runtime. AppStartMP reads
# SYS:\config\*.cfg + SYS:\batch\TNC640heros.txt and writes SYS:\runtime\AppStartFinishCounter.txt.
SYSW=/var/tmp/sysw; sudo rm -rf "$SYSW"; sudo mkdir -p "$SYSW/runtime"
sudo cp -aL "$CFG/config" "$SYSW/config" 2>/dev/null; sudo cp -aL "$CFG/batch" "$SYSW/batch" 2>/dev/null
# AppStartMP's PLIB++ GUI init loads %SYS%\resource\{keymap,charmap,functionkeymap}_us101.xml via
# PReplacePath(%SYS% -> getenv SYS=/tmp/s) + FVolumePathname::Convert (\->/) => /tmp/s/resource/*.
# Stage the control's resource dir so the default keyboard/char/function-key maps load (the PLIB++ wall).
sudo cp -aL "$CFG/resource" "$SYSW/resource" 2>/dev/null
echo "  staged SYS resource files: $(ls "$SYSW/resource" 2>/dev/null | wc -l) (keymap_us101.xml present: $([ -e "$SYSW/resource/keymap_us101.xml" ] && echo yes || echo NO))"
sudo chmod -R u+w "$SYSW/runtime"
ln -sfn "$SYSW" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
# config-#6 fix prerequisites: the jhDataFiles resolve via the SYS:/PLC: VOLUMES (/etc/jhvolume) to
# /mnt/sys/config + /mnt/plc/config, and cfgfix classifies IsSysFile/IsOemFile by those resolved prefixes.
# Stage both volume targets + the productid cache (controlmark=16 = GetOptionTableTnc640).
sudo mkdir -p /mnt/sys/config /mnt/plc/config /mnt/sys/cache/nckern/productid
sudo cp -aL "$CFG/config/." /mnt/sys/config/ 2>/dev/null
[ -f /mnt/plc/config/configfiles.cfg ] || sudo cp -aL "$CFG/default/oem/config/." /mnt/plc/config/ 2>/dev/null
for kv in controlmark:16 exportversion:0 ncstate:1 progstationversion:1 virtualmachine:1; do
  printf "%s\n" "${kv#*:}" | sudo tee /mnt/sys/cache/nckern/productid/${kv%:*}.conf >/dev/null; done
sudo chmod -R a+rX /mnt/sys/config /mnt/plc/config /mnt/sys/cache
echo "  config-#6 fix: /mnt/sys/config ($(ls /mnt/sys/config 2>/dev/null|wc -l)) + /mnt/plc/config ($(ls /mnt/plc/config 2>/dev/null|wc -l)) staged, controlmark=16"
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sudo pkill -x Xvfb 2>/dev/null; sudo pkill -x openbox 2>/dev/null
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm /dev/shm/hrctlU501 /dev/shm/hregU501_* 2>/dev/null

echo "=== [3] start Xvfb $DISP + openbox (native aarch64, outside the ns) ==="
Xvfb $DISP -screen 0 1280x1024x16 -nolisten tcp >/tmp/xvfb.log 2>&1 & XVFB=$!
sleep 2
DISPLAY=$DISP openbox >/tmp/openbox.log 2>&1 & OB=$!
sleep 2
echo "  Xvfb pid $XVFB, openbox pid $OB; X socket: $(ls /tmp/.X11-unix/ 2>/dev/null)"

echo "=== [4] guard: real /etc baseline ==="
B_PASSWD=$(sudo md5sum /etc/passwd | cut -d' ' -f1); echo "  /etc/passwd md5 = $B_PASSWD"

LOG=/tmp/appstart.log; sudo rm -f "$LOG"
# HeROS RTOS env (heros_rtos serves these) + AppStartMP specifics.
read -r -d '' NSCMD <<EOF
set -u
ulimit -c 0
mount --make-rprivate /
mount --bind $R/etc /etc
mount -t tmpfs tmpfs /run 2>/dev/null
mkdir -p /run/dbus /var/run/dbus /var/lib/dbus /etc/dbus-1
mkdir -p /var/run/auth_daemon/fs_mount /var/run/auth_daemon/certs /tmp/auth_daemon /mnt/auth_daemon
chmod 777 /tmp/auth_daemon /mnt/auth_daemon 2>/dev/null
mkdir -p /etc/sysconfig/heuseradmin /etc/security /etc/sysconfig/heros-auth-daemon
[ -e /etc/netgroup ] || : > /etc/netgroup
[ -s /etc/machine-id ] || printf '0123456789abcdef0123456789abcdef\n' > /etc/machine-id
ln -sf /etc/machine-id /var/lib/dbus/machine-id
[ -e /dev/fuse ] || { mknod /dev/fuse c 10 229 2>/dev/null; chmod 666 /dev/fuse 2>/dev/null; }
export PATH=$R/usr/bin:$R/bin:$R/sbin:\$PATH
export LANG=C LC_ALL=C LD_LIBRARY_PATH=$FEXLIBS
# HeROS identity/partition env (served by heros_rtos Sys_getenv)
export SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch
export SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:
export HEROSROOT=$R/heros5 DISPLAY=$DISP
export HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500 HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1
# fontconfig: PLIB++ loads fonts; supply the host's fontconfig into the bound rootfs /etc so the
# i386 fontconfig lib finds a config (else "Cannot load default config file" may stall GUI init).
mkdir -p /etc/fonts
[ -e /etc/fonts/fonts.conf ] || cp -aL /var/tmp/host_fonts.conf /etc/fonts/fonts.conf 2>/dev/null || cat > /etc/fonts/fonts.conf <<'FC'
<?xml version="1.0"?>
<!DOCTYPE fontconfig SYSTEM "fonts.dtd">
<fontconfig><dir>/usr/share/fonts</dir><dir>/usr/X11R6/lib/X11/fonts</dir><cachedir>/tmp/fontcache</cachedir></fontconfig>
FC
export FONTCONFIG_PATH=/etc/fonts FONTCONFIG_FILE=/etc/fonts/fonts.conf
cd /
# %SYS%/%OEM%/%USR% percent-macros are NOT expanded by the control standalone (PReplacePath's macro
# table is unpopulated without the full config/boot) -> AppStartMP opens the LITERAL path
# "%SYS%/resource/keymap_us101.xml" (strace-confirmed ENOENT, same gate as config #6's %SYS% paths).
# Relative to cwd=/, satisfy the literal macro dir with a symlink to the staged SYS/OEM mirror.
ln -sfn /tmp/s "/%SYS%"; ln -sfn /tmp/o "/%OEM%"; ln -sfn /tmp/s "/%USR%"

echo '### foundation: dbus + auth-daemon + heuserver (bg) ###'
LD_PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so FEXInterpreter $R/usr/bin/dbus-daemon --system --nofork --nopidfile --nosyslog >/tmp/a_dbus.log 2>&1 &
sleep 3
cat > /etc/sysconfig/heros-auth-daemon/daemon.conf <<CFG
[daemon]
fuse_mountpoint = /var/run/auth_daemon/certs/
srv_socket = /var/run/auth_daemon/auth-daemon-srv.sock
log_rule = *.debug=false\n;

[misc_data_mount]
root_path = /var/run/auth_daemon/fs_mount/
CFG
LD_PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so FEXInterpreter $R/usr/sbin/heros-auth-daemon -d -c /etc/sysconfig/heros-auth-daemon/daemon.conf -p /var/run/auth_daemon/heros-auth-daemon.pid -l /tmp/a_authd.log >/tmp/a_authd_out.log 2>&1 &
sleep 4
LD_PRELOAD=/lib/herosapi_shim.so:/lib/renamefix.so:/lib/fexunmask.so FEXInterpreter $R/usr/sbin/heuserver >/tmp/a_heu.log 2>&1 &
sleep 5
echo '  heuserver listening:' \$( (ss -ltn 2>/dev/null||true) | grep -c ':19093' )

echo '### ConfigServer (bg) — AppStartMP is a CONFIG CLIENT; it must answer AppStartMP CfgServerQueue ###'
# ConfigServer must be task 0x100 (its hardcoded run-up); it starts BEFORE AppStartMP so it owns the
# real CfgServerQueue (not an AppStartMP black-hole). Then AppStartMP's config query reaches it (+INJECT_ACK).
( timeout -s KILL 120 env LD_PRELOAD=/lib/cfgfix.so:/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so \
    CFGFIX_SYS=/mnt/sys/ CFGFIX_OEM=/mnt/plc/ \
    FEXInterpreter $R/heros5/bin/ConfigServer.elf -p=~/cfgserver cfgserver \
    -f=/tmp/s/config/jhconfigfiles.cfg -i=Nc 2>&1 | head -c 60000000 > /tmp/a_cfgsrv.log ) &
i=0; while [ \$i -lt 120 ]; do grep -q "HWS stub: replied" /tmp/a_cfgsrv.log 2>/dev/null && break; sleep 0.5; i=\$((i+1)); done
sleep 4
echo '  ConfigServer run-up (HWS stub):' \$(grep -c "HWS stub: replied" /tmp/a_cfgsrv.log 2>/dev/null) ' main task:' \$(grep -m1 "task_self -> new id" /tmp/a_cfgsrv.log|sed "s/.*new id //;s/ .*//")

echo '### AppStartMP (fg, ~45s) — RTOS process-manager; spawns the constellation ###'
# AppStartMP issues heroscalls (libheros.so.1) -> needs heros_rtos; arena_stub bridges arena_exclusive.
# Run-1 known-good preload set (NO openlog guest preload — it perturbed the timing-sensitive startup
# so AppStartMP never connected). Capture the EXACT keymap/resource file paths NON-invasively via
# host strace (proven to see FEX guest syscalls): openat/newfstatat/access -> /tmp/a_strace.log.
rm -f /tmp/a_strace.log
# HEROS_EVENTS_PIPE=1: back /dev/events with a blocking pipe (not an always-ready memfd) so the libbackend
# EVHandler dispatcher select()-blocks instead of busy-spinning on ev_receive(0x01011001) (1.5M polls).
# Trace connect (X-socket attempt) + select (the block) + openat to see if AppStartMP then advances.
# NOTE: write to the log via a REDIRECT, not "| head" — when timeout SIGKILLs strace, FEXInterpreter
# DETACHES (tracee survives a dead tracer) and keeps the pipe write-end open, so "| head" hangs forever.
# A redirect lets timeout return; the explicit pkill below reaps the detached FEX. (Spin is gone now, so
# the log is small — no head cap needed.)
# HEROS_EVENTS_PIPE=1: /dev/events = blocking pipe (kills the busy-spin) -> AppStartMP connects to X +
# spawns the LogoModule thread, then cleanly blocks on Ev_receive(0x01019007) before the constellation spawn.
# Experimental knobs (default OFF; see herosapi_shim.c / heros_rtos.c):
#   HEROSCALL_SELECT_CAP_MS=N  caps the dispatcher select() (no effect on this gate — the block is a heros
#                              event-wait, not select).
#   HEROSCALL_EV_UNBLOCK_MS=N  forces forever event-waits to return after N ms (direct event injection).
#                              Drives the AppStart::Monitor sequencer BUT returning the full want-mask trips
#                              FWaitableInput::Unmask "0 < mask" (fwaitable.cpp:248) — needs the precise
#                              single awaited waitable bit (RE the Monitor's waitable to use safely).
timeout -s KILL 120 /usr/bin/strace -f -qq -e trace=openat,connect,execve,clone,fork,vfork -o /tmp/a_strace.log \
  env HEROS_EVENTS_PIPE=1 \
  LD_PRELOAD=/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so \
  FEXInterpreter \$R/heros5/bin/AppStartMP.elf /tmp/s/batch/TNC640heros.txt >/tmp/a_appstart.log 2>&1
pkill -KILL -x strace 2>/dev/null; pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
echo "### AppStartMP exited (rc \$?) ###"
rm -f "/%SYS%" "/%OEM%" "/%USR%" 2>/dev/null
pkill -KILL -x FEXInterpreter 2>/dev/null
EOF

echo "=== [5] run (contained) ==="
sudo unshare -m bash -c "$NSCMD" >"$LOG" 2>&1
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; kill $XVFB $OB 2>/dev/null; sudo pkill -x Xvfb 2>/dev/null; sudo pkill -x openbox 2>/dev/null

echo "=== [6] guard: real /etc unchanged? ==="
A_PASSWD=$(sudo md5sum /etc/passwd | cut -d' ' -f1)
[ "$A_PASSWD" = "$B_PASSWD" ] && echo "  SAFE: /etc/passwd md5 unchanged" || echo "  *** WARNING: /etc/passwd CHANGED ($B_PASSWD -> $A_PASSWD) ***"

echo "=== [7] AppStartMP RESULTS ==="
grep -vE "cannot be preloaded|object .* from LD_PRELOAD" /tmp/a_appstart.log 2>/dev/null > /tmp/a_clean.log
echo "--- ConfigServer served AppStartMP's config query? (INJECT_ACK / Q_read of AppStart's queue) ---"
grep -E "INJECT_ACK|CfgServerQueue|0000101CfgM" /tmp/a_cfgsrv.log /tmp/a_clean.log 2>/dev/null | grep -iE "inject|read <-|0101cfgm" | head -6
echo "--- PLIB++ keymap wall: cleared? (was: Unable to load the default keyboard map) ---"
grep -iE "keymap|charmap|functionkey|keyboard map|character map|function key map|resource symbol" /tmp/a_clean.log | head -10
echo "--- event pump: busy-spin gone? (Ev_receive(01011001) poll count — was 1.5M) ---"
echo "    Ev_receive(01011001) polls: $(grep -ac 'Ev_receive ] p=\[01011001' /tmp/a_appstart.log 2>/dev/null)"
echo "--- /dev/events backing (pipe vs memfd) ---"
grep -aE "faking open\(\"/dev/events" /tmp/a_appstart.log 2>/dev/null | head -2
echo "--- X-connect attempt? (connect to X11 socket) + select blocks ---"
grep -aiE "connect\(.*X11|connect\(.*7000|connect\(.*99" /tmp/a_strace.log 2>/dev/null | head -5
echo "    select/poll calls (the dispatcher block): $(grep -acE '_newselect|pselect6|[^a-z]select\(|poll\(' /tmp/a_strace.log 2>/dev/null)"
echo "--- last 8 distinct strace syscalls (where it ends up) ---"
tail -40 /tmp/a_strace.log 2>/dev/null | grep -aoE "^[0-9]+ +[a-z_]+\(" | sed -E "s/^[0-9]+ +//" | sort | uniq -c | sort -rn | head -8
echo "--- X / WindowManager wait passed? ---"
grep -iE "X-Server|X-Window|waiting for|PLIB" /tmp/a_clean.log | head -8
echo "--- ★ CONSTELLATION SPAWN: did AppStartMP execve any subsystem process? ---"
echo "    execve in strace: $(grep -ac execve /tmp/a_strace.log 2>/dev/null)"
grep -aE "execve\(" /tmp/a_strace.log 2>/dev/null | grep -avE "AppStartMP\.elf" | sed -E "s/.*execve\(//; s/, \[.*//" | sort -u | head -20
echo "--- spawn/subsystem text + heuseradmin connect outcome ---"
grep -iE "heuseradmin|stream socket|connection refused|connected|spawn|FmLoadProcess|FmLoadSubsystem|Subsystem: |fork|launch|PCreate" /tmp/a_clean.log | head -15
echo "--- next blocker / errors ---"
grep -iE "error|cannot|failed|refused|abort|assert|not found|denied|timeout" /tmp/a_clean.log | grep -viE "sigchild|debug" | head -15
echo "--- AppStartMP own output (non-trace) ---"
grep -vE "^\[rtos\]|^\[t[0-9]|FULL\[|^   [0-9a-f]|^\[herosapi" /tmp/a_clean.log | head -30
echo "--- last RTOS calls (what AppStartMP blocks on) ---"
grep -E "^\[rtos\]|^\[t[0-9]" /tmp/a_clean.log | tail -12
echo "--- did AppStartMP connect to Xvfb? (X server log) ---"
grep -iE "client|connect|reset" /tmp/xvfb.log 2>/dev/null | tail -4
echo "logs: /tmp/appstart.log /tmp/a_appstart.log /tmp/a_heu.log /tmp/xvfb.log /tmp/openbox.log"