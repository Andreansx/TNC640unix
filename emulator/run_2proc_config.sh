#!/bin/sh
# run_2proc_config.sh — the 2-process config experiment (blocker #5).
#
# Runs ConfigServer.elf and ipo_progstation.elf together under the HeROS RTOS
# runtime, sharing ONE /dev/shm/heros_rtos_ctl namespace (the runtime's futexes are
# process-SHARED, not PRIVATE, so cross-process wakeups work). ConfigServer creates
# the service queues first; IPO then attaches the same namespace.
#
# PROVEN RESULT: IPO resolves "CfgServerQueue" across processes (Q_ident) and Q_sends
# its config request to the live ConfigServer — past the standalone blocker #5 (no
# more "Invalid Command Option -k" / err 42). IPO then Q_reads its reply queue waiting
# for the answer. OPEN: ConfigServer's serve loop for CfgServerQueue is not yet active
# (its main thread parks in a glibc futex after init, before signalling the workers to
# serve), so the request sits unread. See docs/17-heroscall-emulator.md.
#
# Only ConfigServer clears the shared segment; IPO must NOT (it attaches an existing one).
set -e
SYSROOT=${SYSROOT:-$HOME/tnc/sysroot}
ORACLES=${ORACLES:-$HOME/tnc/oracles}
CFGTREE=${CFGTREE:-$HOME/tnc/SYS}
SHIMDIR=${SHIMDIR:-$(cd "$(dirname "$0")" && pwd)}
LP="$SYSROOT/heros5/bin:$ORACLES:$SYSROOT/lib:$SYSROOT/usr/lib"
LD="$SYSROOT/lib/ld-linux.so.2"
PRE="$SHIMDIR/herosapi_shim.so $SHIMDIR/heros_rtos.so"
ln -sfn "$CFGTREE" /tmp/s; ln -sfn "$CFGTREE/default/oem" /tmp/o; ln -sfn "$SYSROOT/heros5/bin" /tmp/b
rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* 2>/dev/null || true
head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0644 /dev/shm/_heusrv_shm

export SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o
export EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5
export SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:
export HEROSCALL_VERBOSE=1 HEROSCALL_SEM_INIT=1

echo "### ConfigServer (background, creates the shared namespace + queues) ###"
timeout 45 env LD_PRELOAD="$PRE" "$LD" --library-path "$LP" \
  "$SYSROOT/heros5/bin/ConfigServer.elf" '-p=~/cfgserver' cfgserver \
  "-f=/tmp/s/config/jhconfigfiles.cfg" -i=Nc > /tmp/cfgsrv.log 2>&1 &
CFGPID=$!
for i in $(seq 1 24); do
  grep -q 'Q_create "CfgServerQueue"' /tmp/cfgsrv.log 2>/dev/null && break
  sleep 0.5
done
echo "ConfigServer queues:"; grep -E 'Q_create "(CfgServerQueue|CfgFileMan)"' /tmp/cfgsrv.log | sed 's/.*rtos] //'

echo "### IPO (foreground, attaches the SAME namespace; does NOT clear it) ###"
timeout 25 env LD_PRELOAD="$PRE" "$LD" --library-path "$LP" \
  "$SYSROOT/heros5/bin/ipo_progstation.elf" '-p=~/IPO' IPO -k=NC -M > /tmp/ipo2.log 2>&1
echo "IPO exit=$?"
echo "IPO -> CfgServerQueue:"; grep -E 'Q_ident "CfgServerQueue"|Q_send -> queue' /tmp/ipo2.log | tail -4
kill $CFGPID 2>/dev/null || true; wait $CFGPID 2>/dev/null || true
