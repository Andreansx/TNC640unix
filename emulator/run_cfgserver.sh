#!/bin/sh
# run_cfgserver.sh — the 2-process config-server experiment (docs/17 "Blocker #5").
# Runs the genuine HeROS ConfigServer.elf under the heroscall emulator, pointing
# SYS/OEM/USR at a config tree (the extracted control's config/ + default/oem/).
#
# Findings: ConfigServer parses its options and sets up async signals, then blocks
# in the FProcess startup-sync barrier (infinite Ev_receive for event 0x80000).
# HEROSCALL_GRANT_EVENTS=1 pushes past it, but FThread::EvalContextThread then
# asserts "Context could not be created" — the genuine multi-threaded server needs
# a faithful HeROS task/context/event runtime, not coarse stubs.
set -e
SYSROOT=${SYSROOT:-$HOME/tnc/sysroot}
ORACLES=${ORACLES:-$HOME/tnc/oracles}       # all 248 control .so (library path)
CFGTREE=${CFGTREE:-$HOME/tnc/SYS}           # extracted control config tree
SHIMDIR=${SHIMDIR:-$(cd "$(dirname "$0")" && pwd)}

export SYS=$CFGTREE OEM=$CFGTREE/default/oem OEME=$CFGTREE/default/oem USR=$CFGTREE
export EXECDIRH=$SYSROOT/heros5/bin EXECDIR=$SYSROOT/heros5/bin EXECBAT=$CFGTREE/batch/heros5
export SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:
# export HEROSCALL_VERBOSE=1 HEROSCALL_GRANT_EVENTS=1 HEROSAPI_LOGOPEN=1   # diagnostics

exec env LD_PRELOAD="$SHIMDIR/herosapi_shim.so $SHIMDIR/heroscall_emu.so" \
  "$SYSROOT/lib/ld-linux.so.2" \
  --library-path "$SYSROOT/heros5/bin:$ORACLES:$SYSROOT/lib:$SYSROOT/usr/lib" \
  "$SYSROOT/heros5/bin/ConfigServer.elf" '-p=~/cfgserver' cfgserver \
  "-f=$CFGTREE/config/jhconfigfiles.cfg" -i=Nc "$@"
