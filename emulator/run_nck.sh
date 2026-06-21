#!/bin/sh
# run_nck.sh — run the i386 TNC640 NCK (ipo_progstation.elf) natively on an
# x86_64 host (or under user-mode translation on ARM64) with the HeROS heroscall
# emulator LD_PRELOADed. See docs/17-heroscall-emulator.md.
#
# Prereqs (built next to this script, i386 shared objects):
#   herosapi_shim.so   gcc -m32 -shared -fPIC -O2 herosapi_shim.c
#   heroscall_emu.so   gcc -m32 -shared -fPIC -O2 heroscall_emu.c
# and a combined sysroot $SYSROOT = HeROS rootfs (glibc 2.31 + system libs)
# with the control's heros5/bin grafted in.
set -e
SYSROOT=${SYSROOT:-$HOME/tnc/sysroot}
SHIMDIR=${SHIMDIR:-$(cd "$(dirname "$0")" && pwd)}

# HeROS environment, lifted verbatim from the control's own boot scripts
# (heros5/bin/../application + appproduct). These are what Sys_getenv must return.
export SYS=/mnt/sys OEM=/mnt/plc OEME=/mnt/plce USR=/mnt/tnc
export EXECDIRH=/mnt/sys/heros5/bin EXECDIR=/mnt/sys/heros5/bin EXECBAT=/mnt/sys/batch/heros5
export SYS_NAME=SYSTEM: OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:

# argv recovered from the AppStart batch (batch/TNC640heros.txt): the programming
# station's IPO is "FmLoadProcess(processName=~/IPO, options=-k=NC -M)". The
# FProcess framework wants:  -p=<procname> <name> <app-options...>
exec env LD_PRELOAD="$SHIMDIR/herosapi_shim.so $SHIMDIR/heroscall_emu.so" \
  "$SYSROOT/lib/ld-linux.so.2" \
  --library-path "$SYSROOT/heros5/bin:$SYSROOT/lib:$SYSROOT/usr/lib" \
  "$SYSROOT/heros5/bin/ipo_progstation.elf" '-p=~/IPO' IPO -k=NC -M "$@"
