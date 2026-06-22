#!/bin/sh
# run_2proc_arm64.sh — the IPO<->ConfigServer 2-process config experiment under
# qemu-i386 on an Apple-Silicon ARM64 host (lima VM `tnc`). This is the native
# x86_64 box's run_2proc_config.sh ported to the project's ACTUAL target: the
# i386 control + heroscall emulator, translated to aarch64 by qemu-user.
#
# Prereqs (built INSIDE the VM, i386 shared objects, into /tmp — the host mount is RO):
#   i686-linux-gnu-gcc -shared -fPIC -O2 -o /tmp/heros_rtos.so   emulator/heros_rtos.c
#   i686-linux-gnu-gcc -shared -fPIC -O2 -o /tmp/herosapi_shim.so emulator/herosapi_shim.c
# Run from the VM:  limactl shell tnc -- sh <repo>/emulator/run_2proc_arm64.sh
set -e
REPO=${REPO:-/Users/andreansx/Documents/TNC640unix}
R="$REPO/work/target/rootfs"                 # combined i386 sysroot (glibc 2.31 + heros5 graft)
CFG="$REPO/work/control/sysroot"             # SYS partition (config/jhconfigfiles.cfg lives here)
# LD_PRELOAD uses COLON separators (space-separated would word-split through qemu -E)
PRE="/tmp/herosapi_shim.so:/tmp/heros_rtos.so"

ln -sfn "$CFG" /tmp/s; ln -sfn "$CFG/default/oem" /tmp/o; ln -sfn "$R/heros5/bin" /tmp/b
rm -f /dev/shm/heros_rtos_ctl /dev/shm/heros_reg_* /dev/shm/_heusrv_shm 2>/dev/null || true
head -c 1048576 /dev/zero > /dev/shm/_heusrv_shm; chmod 0644 /dev/shm/_heusrv_shm

# qemu env: HeROS partitions/identity + emulator knobs. SYNC_TIMEOUT caps the absent
# SikServer handshake so startup advances past SIK (see docs/17).
ENVS="-E SYS=/tmp/s -E OEM=/tmp/o -E USR=/tmp/s -E OEME=/tmp/o \
-E EXECDIRH=/tmp/b -E EXECDIR=/tmp/b -E EXECBAT=/tmp/s/batch/heros5 \
-E SYS_NAME=SYSTEM: -E OEM_NAME=PLC: -E OEME_NAME=PLCE: -E USR_NAME=TNC: \
# NB: HEROSCALL_REPLAY_TRIGGER (capture+replay CfgServerQueue startup msgs) is OFF — it does NOT work:
# the connect-ACK flow (SIK thread + in-process EditThread clients → SendConnected) is entirely
# INTERNAL, so there's no capturable queue message (trigger or ACK) to echo. IPO (first EXTERNAL
# client) needs the absent MMI's UpdNewState, which must be CONSTRUCTED. See docs/17.
-E HEROSCALL_VERBOSE=1 -E HEROSCALL_SEM_INIT=1 -E HEROSCALL_SYNC_TIMEOUT=2500 -E HEROSCALL_HWS_STUB=1 -E HEROSCALL_TIMERS=1 \
-E LD_PRELOAD=$PRE -E LD_LIBRARY_PATH=/heros5/bin:/lib:/usr/lib"
QEMU="qemu-i386 -L $R $ENVS $R/lib/ld-linux.so.2"

echo "### ConfigServer (background, creates the shared namespace + queues) ###"
( timeout -s KILL 150 $QEMU "$R/heros5/bin/ConfigServer.elf" -p=~/cfgserver cfgserver \
   -f=/tmp/s/config/jhconfigfiles.cfg -i=Nc 2>&1 | head -c 80000000 > /tmp/cfgsrv.log ) &
CFGPID=$!
# Wait until ConfigServer has FINISHED its run-up before connecting IPO — the dispatch
# loop only binds/answers client connects once it reaches steady state. Connecting too
# early (when CfgServerQueue merely exists) makes the connect get drained-without-reply
# during startup. The HWS stub reply is the last run-up step; after it, ConfigServer
# settles into the CfgServerQueue dispatch loop. (Real systems start clients after the
# server is up — AppStartMP ordering.)
# Wait for ConfigServer's run-up to finish before connecting IPO (its dispatch loop only serves
# after run-up). NB: early-connect does NOT help — the dispatch processes connects only post-run-up,
# i.e. AFTER the one-time SIK-flow SendConnected flush; so IPO is registered too late to be ACKed
# regardless of when it connects. The connect-ACK needs a STEADY-STATE flush trigger = the absent
# MMI's UpdNewState (a host-side peer). See docs/17.
i=0; while [ $i -lt 300 ]; do
  grep -q 'HWS stub: replied' /tmp/cfgsrv.log 2>/dev/null && break
  sleep 0.5; i=$((i+1))
done
sleep 5
echo "ConfigServer queues:"; grep -E 'Q_create "(CfgServerQueue|CfgFileMan)"' /tmp/cfgsrv.log 2>/dev/null | sed 's/.*rtos] //' || true
echo "ConfigServer run-up done (HWS stub fired): $(grep -c 'HWS stub: replied' /tmp/cfgsrv.log 2>/dev/null)"

echo "### IPO (foreground, attaches the SAME namespace; does NOT clear it) ###"
timeout -s KILL 80 $QEMU "$R/heros5/bin/ipo_progstation.elf" -p=~/IPO IPO -k=NC -M > /tmp/ipo2.log 2>&1 || true
echo "IPO done"
kill $CFGPID 2>/dev/null || true; wait $CFGPID 2>/dev/null || true
echo "=== logs: /tmp/cfgsrv.log /tmp/ipo2.log ==="
