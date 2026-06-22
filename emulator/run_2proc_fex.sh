#!/bin/bash
# run_2proc_fex.sh — the IPO <-> ConfigServer 2-process config experiment under FEX on ARM64.
#
# This is run_2proc_arm64.sh (qemu-i386) ported to FEX, to confirm the LAST technical piece of the
# multi-process constellation under FEX: CROSS-PROCESS shared-memory futexes. heros_rtos.c keeps its
# control segment + registered regions in /dev/shm and synchronizes the two processes (T_create/
# T_start rendezvous, Q_send/Q_read, the connect handshake) with futexes on that shared memory. Under
# qemu-i386 this worked (blocker #5 "IPO CONNECTS"); the open question is whether FEX translates futex()
# on shared memory across two INDEPENDENT FEXInterpreter processes. If IPO prints "Connected" here, the
# whole multi-process model is proven under one translator.
#
# Both processes run in ONE mount namespace so they share /dev/shm + /tmp (the cross-process namespace).
# rootfs /etc is bound over /etc to contain any /etc writes (the FEX /etc-leak lesson — protect the VM).
# Run in VM tnc:  limactl shell tnc -- bash <repo>/emulator/run_2proc_fex.sh
set -u
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$REPO/work/target/rootfs"
CFG="$REPO/work/control/sysroot"          # SYS partition (config/jhconfigfiles.cfg)
R=/var/tmp/lr                              # FEX rootfs (must already hold the ConfigServer/IPO closure)
CC=i686-linux-gnu-gcc
PRE="/lib/arena_stub.so:/lib/herosapi_shim.so:/lib/heros_rtos.so"

echo "=== [1] preflight: binaries + preloads present ==="
for b in heros5/bin/ConfigServer.elf heros5/bin/ipo_progstation.elf usr/lib/libheros.so.1; do
  [ -e "$R/$b" ] || { echo "  MISSING $R/$b — run the closure copy first"; exit 1; }
done
# (re)build the i386 preloads fresh from source into the rootfs /lib
$CC -shared -fPIC -O2 -Wl,--version-script="$REPO/emulator/arena.map" -o "$R/lib/arena_stub.so" "$REPO/emulator/arena_stub.c" || exit 1
for s in herosapi_shim heros_rtos renamefix; do
  $CC -shared -fPIC -O2 -o "$R/lib/$s.so" "$REPO/emulator/$s.c" || exit 1
done
echo "  preloads rebuilt: arena_stub herosapi_shim heros_rtos renamefix"

echo "=== [2] config symlinks + clean shared namespace ==="
ln -sfn "$CFG" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
# Clean any leftover RTOS shared state so this is a fresh namespace (the futex rendezvous needs it).
# CRITICAL: the control segment is created by ConfigServer running under `sudo unshare`, so it is
# ROOT-owned — clean it with sudo, else a stale counter makes ConfigServer's main task != 0x100 and
# its hardcoded-0x100 run-up (AppStartMaster owner) breaks (no HWS stub, no CfgServerQueue).
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm 2>/dev/null
if ls /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* >/dev/null 2>&1; then
  echo "  !!! /dev/shm not clean — stale RTOS segment survives"; ls -la /dev/shm/ | grep -i heros; exit 1
fi
echo "  /dev/shm clean (fresh control segment -> ConfigServer will be task 0x100)"
sudo bash -c 'head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0644 /dev/shm/_heusrv_shm'
ls -la /tmp/s/config/jhconfigfiles.cfg >/dev/null 2>&1 || { echo "  config not reachable via /tmp/s"; exit 1; }

# HeROS identity/partition env (served by heros_rtos's Sys_getenv) + the emulator knobs that carried
# the qemu-i386 run to the connect (INJECT_ACK synthesizes IPO's CfgClientIsConnected — blocker #5).
ENVCOMMON=(
  SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o
  EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5
  SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:
  HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1 HEROSCALL_SYNC_TIMEOUT=2500
  HEROSCALL_HWS_STUB=1 HEROSCALL_TIMERS=1 HEROSCALL_INJECT_ACK=1
  LANG=C LC_ALL=C LD_PRELOAD="$PRE"
)

echo "=== [3] /etc/passwd guard (detect any VM corruption) ==="
GUARD_BEFORE=$(md5sum /etc/passwd | awk '{print $1}'); echo "  real /etc/passwd md5 = $GUARD_BEFORE"

echo "=== [4] run ConfigServer (bg) + IPO (fg) in ONE mount-ns under FEX ==="
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo env R="$R" PRE="$PRE" "${ENVCOMMON[@]}" unshare -m bash -c '
  set -u
  ulimit -c 0
  mount --make-rprivate /
  mount --bind "$R/etc" /etc                       # contain /etc writes (protect the VM)
  # FEXInterpreter is dynamic; with /etc bound the ld.so.cache no longer lists its aarch64 libs.
  export LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu
  cd /

  echo "### ConfigServer (background: creates the shared namespace + queues) ###"
  ( timeout -s KILL 150 FEXInterpreter "$R/heros5/bin/ConfigServer.elf" \
      -p=~/cfgserver cfgserver -f=/tmp/s/config/jhconfigfiles.cfg -i=Nc 2>&1 \
      | head -c 80000000 > /tmp/cfgsrv_fex.log ) &
  CFGPID=$!

  # Wait until ConfigServer finishes its run-up (HWS stub is the last run-up step) before connecting IPO.
  i=0
  while [ $i -lt 240 ]; do
    grep -q "HWS stub: replied" /tmp/cfgsrv_fex.log 2>/dev/null && { echo "  ConfigServer run-up complete (HWS stub) at ${i}*0.5s"; break; }
    sleep 0.5; i=$((i+1))
  done
  sleep 5
  echo "ConfigServer main task (expect 0x100): $(grep -m1 "task_self -> new id" /tmp/cfgsrv_fex.log 2>/dev/null | sed "s/.*new id //;s/ .*//")"
  echo "ConfigServer queues:"
  grep -E "Q_create \"(CfgServerQueue|CfgFileMan|QEvtServer|AppStartMaster)\"" /tmp/cfgsrv_fex.log 2>/dev/null | sed "s/.*rtos] //" | head
  echo "ConfigServer run-up (HWS stub fired): $(grep -c "HWS stub: replied" /tmp/cfgsrv_fex.log 2>/dev/null)"
  echo "Tasks created: $(grep -c "T_create" /tmp/cfgsrv_fex.log 2>/dev/null) ; M_attach: $(grep -c "M_attach" /tmp/cfgsrv_fex.log 2>/dev/null)"

  echo "### IPO (foreground: attaches the SAME /dev/shm namespace; cross-process futex test) ###"
  timeout -s KILL 80 FEXInterpreter "$R/heros5/bin/ipo_progstation.elf" \
      -p=~/IPO IPO -k=NC -M > /tmp/ipo2_fex.log 2>&1 || true
  echo "### IPO done ###"
  kill $CFGPID 2>/dev/null; wait $CFGPID 2>/dev/null
'
RC=$?
sudo pkill -KILL -x FEXInterpreter 2>/dev/null

echo "=== [5] /etc/passwd guard recheck ==="
GUARD_AFTER=$(md5sum /etc/passwd | awk '{print $1}')
if [ "$GUARD_BEFORE" = "$GUARD_AFTER" ]; then echo "  OK — /etc/passwd unchanged ($GUARD_AFTER)"
else echo "  !!! /etc/passwd CHANGED ($GUARD_BEFORE -> $GUARD_AFTER) — containment failed"; fi

echo "=== [6] RESULTS ==="
echo "--- ConfigServer: RTOS run-up evidence ---"
grep -E "control segment|T_create|T_start|Q_create \"(CfgServerQueue|QEvtServer)\"|M_attach|HWS stub: replied" /tmp/cfgsrv_fex.log 2>/dev/null | sed "s/.*rtos] //" | head -16
echo "--- IPO: connect outcome (IPO's own output, excluding rtos/emulator trace) ---"
grep -vE "^\[rtos\]|^\[t[0-9]|FULL\[|^   [0-9a-f]" /tmp/ipo2_fex.log 2>/dev/null \
  | grep -iE "connected|connect|CheckOptions|Invalid Command|AskIpoConditions|channel|abort|assert|condition" | head -20
echo "--- emulator: INJECT_ACK posted? (the synthesized connect-ACK to IPO's reply queue) ---"
grep "INJECT_ACK" /tmp/ipo2_fex.log 2>/dev/null | head -3
echo
# IPO's OWN "Connected" print = success; the rtos "CfgClientIsConnected" line is the emulator, not IPO.
if grep -vE "^\[rtos\]|^\[t[0-9]" /tmp/ipo2_fex.log 2>/dev/null | grep -qiw "Connected"; then
  echo "==> ★ IPO PRINTS 'Connected' UNDER FEX — cross-process connect handshake works under one translator."
elif grep -q "INJECT_ACK" /tmp/ipo2_fex.log 2>/dev/null; then
  echo "==> Partial: emulator posted the connect-ACK across processes (shared /dev/shm queue works), but IPO's"
  echo "    'Connected' print not captured (likely cut by timeout). Cross-process queue path engaged."
else
  echo "==> IPO did not reach the connect; inspect /tmp/cfgsrv_fex.log + /tmp/ipo2_fex.log"
fi
echo "logs: /tmp/cfgsrv_fex.log  /tmp/ipo2_fex.log  (rc=$RC)"
