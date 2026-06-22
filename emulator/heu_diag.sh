#!/bin/bash
# heu_diag.sh PRELOAD_CSV  — run heuserver once, contained, report exit/shm/last-line.
# PRELOAD_CSV e.g. "herosapi_shim.so:heros_rtos.so:renamefix.so" (rootfs /lib names)
set -u
R=/var/tmp/lr
PL="${1:-herosapi_shim.so:heros_rtos.so:renamefix.so}"
PRE=$(echo "$PL" | sed 's#[^:]*#/lib/&#g')
sudo rm -f /dev/shm/_heusrv_shm /tmp/h.log /tmp/h.exit 2>/dev/null
sudo unshare -m bash -c "
  ulimit -c 0
  mount --make-rprivate /
  mount --bind $R/etc /etc
  mkdir -p /etc/sysconfig/heuseradmin /etc/security; : > /etc/netgroup
  cd /
  timeout -s KILL 15 env LANG=C LC_ALL=C LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
    LD_PRELOAD=$PRE FEXInterpreter $R/usr/sbin/heuserver >/tmp/h.log 2>&1
  echo \$? >/tmp/h.exit
" 2>/dev/null
EC=$(cat /tmp/h.exit 2>/dev/null)
SHM=$([ -e /dev/shm/_heusrv_shm ] && echo YES || echo NO)
echo "PRELOAD=[$PL]  EXIT=$EC  shm_heusrv_created=$SHM"
echo "--- last 4 non-noise lines ---"
grep -vE "cannot be preloaded" /tmp/h.log | tail -4
