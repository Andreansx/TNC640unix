#!/bin/sh
# run_rtos_cfgserver.sh — run the genuine HeROS ConfigServer.elf under the faithful
# RTOS runtime (heros_rtos.so). With the async-signal + named-queue/semaphore +
# HeLogger-degradation fixes, ConfigServer boots its FULL service constellation
# (CfgFileMan, EditThreadQue/Notify, CfgServerQueue depth 100, QSikInterface,
# QDongleService, AppStartMaster, QEvtServer) and settles into a clean idle state
# — no crash, no spin. See docs/17-heroscall-emulator.md.
#
# Two non-heroscall peer dependencies are provisioned host-side here:
#  - /dev/shm/_heusrv_shm : heuseradmin getShm() shm_opens + derefs it; absent -> NULL
#                           deref -> SIGSEGV. A zeroed readable file = empty user DB.
#  - SHORT env paths (/tmp/s ...) : the control sprintf()s $SYS into fixed buffers.
set -e
SYSROOT=${SYSROOT:-$HOME/tnc/sysroot}
ORACLES=${ORACLES:-$HOME/tnc/oracles}
CFGTREE=${CFGTREE:-$HOME/tnc/SYS}
SHIMDIR=${SHIMDIR:-$(cd "$(dirname "$0")" && pwd)}
ln -sfn "$CFGTREE" /tmp/s; ln -sfn "$CFGTREE/default/oem" /tmp/o; ln -sfn "$SYSROOT/heros5/bin" /tmp/b
rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* 2>/dev/null || true
head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0644 /dev/shm/_heusrv_shm

export SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o
export EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5
export SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:
export HEROSCALL_SEM_INIT=1            # auto-provided semaphores start AVAILABLE
# export HEROSCALL_VERBOSE=1          # thread-tagged heroscall trace

exec env LD_PRELOAD="$SHIMDIR/herosapi_shim.so $SHIMDIR/heros_rtos.so" \
  "$SYSROOT/lib/ld-linux.so.2" \
  --library-path "$SYSROOT/heros5/bin:$ORACLES:$SYSROOT/lib:$SYSROOT/usr/lib" \
  "$SYSROOT/heros5/bin/ConfigServer.elf" '-p=~/cfgserver' cfgserver \
  "-f=/tmp/s/config/jhconfigfiles.cfg" -i=Nc "$@"
