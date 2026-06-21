#!/bin/sh
# run_rtos.sh — run a HeROS control process under the faithful RTOS runtime
# (heros_rtos.so: real blocking tasks/events/semaphores/queues + /dev/shm shared
# state). See docs/17-heroscall-emulator.md "The RTOS runtime".
#
#   ./run_rtos.sh ConfigServer.elf -p=~/cfgserver cfgserver -f=$SYS/config/jhconfigfiles.cfg -i=Nc
#   ./run_rtos.sh ipo_progstation.elf -p=~/IPO IPO -k=NC -M
#
# Use SHORT mount-style paths (the control sprintf()s env paths into fixed buffers
# sized for /mnt/sys etc.). Point them at the extracted config tree via symlinks.
set -e
SYSROOT=${SYSROOT:-$HOME/tnc/sysroot}
ORACLES=${ORACLES:-$HOME/tnc/oracles}
CFGTREE=${CFGTREE:-$HOME/tnc/SYS}
SHIMDIR=${SHIMDIR:-$(cd "$(dirname "$0")" && pwd)}
ln -sfn "$CFGTREE" /tmp/s; ln -sfn "$CFGTREE/default/oem" /tmp/o; ln -sfn "$SYSROOT/heros5/bin" /tmp/b
rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* 2>/dev/null || true

export SYS=/tmp/s OEM=/tmp/o USR=/tmp/s OEME=/tmp/o
export EXECDIRH=/tmp/b EXECDIR=/tmp/b EXECBAT=/tmp/s/batch/heros5
export SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:
# export HEROSCALL_VERBOSE=1

BIN=$1; shift
exec env LD_PRELOAD="$SHIMDIR/herosapi_shim.so $SHIMDIR/heros_rtos.so" \
  "$SYSROOT/lib/ld-linux.so.2" \
  --library-path "$SYSROOT/heros5/bin:$ORACLES:$SYSROOT/lib:$SYSROOT/usr/lib" \
  "$SYSROOT/heros5/bin/$BIN" "$@"
