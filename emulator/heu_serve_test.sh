#!/bin/bash
# heu_serve_test.sh — validate heuserver SERVES a client connection on 127.0.0.1:19093.
# Runs heuserver in the FOREGROUND, backgrounded (NOT -d: a daemon holds the ssh channel
# open and hangs limactl). Output redirected to a file so the bg job can't hold the pipe.
# Connects a plain TCP client, reports whether heuserver accepts + handles it (logs
# "New Connection") without crashing — i.e. whether the serving path works and is RTOS-free.
set -u
R=/var/tmp/lr
PRE=/lib/herosapi_shim.so:/lib/renamefix.so
B=$(sudo md5sum /etc/passwd | cut -d' ' -f1)
sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sleep 1
sudo rm -f /dev/shm/_heusrv_shm /tmp/heuserve.log

echo "=== start heuserver (foreground, contained, backgrounded) ==="
sudo unshare -m bash -c "
  ulimit -c 0; mount --make-rprivate /; mount --bind $R/etc /etc
  mkdir -p /etc/sysconfig/heuseradmin /etc/security; : > /etc/netgroup; cd /
  env LANG=C LC_ALL=C LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
    LD_PRELOAD=$PRE FEXInterpreter $R/usr/sbin/heuserver
" >/tmp/heuserve.log 2>&1 &
NSPID=$!
sleep 6
echo "--- listening? ---"; sudo ss -ltnp 2>/dev/null | grep 19093 || { echo "  FAILED to bind"; grep -vE 'cannot be preloaded' /tmp/heuserve.log|tail; sudo kill -9 $NSPID 2>/dev/null; exit 1; }

echo "=== connect a TCP client to 127.0.0.1:19093 and probe ==="
python3 - <<'PY'
import socket, time
try:
    s = socket.create_connection(("127.0.0.1", 19093), timeout=4)
    print(f"  CONNECTED from local port {s.getsockname()[1]}")
    s.sendall(b"\x00\x00\x00\x04TEST")
    time.sleep(0.5); s.settimeout(2)
    try:
        d = s.recv(256); print(f"  heuserver replied {len(d)} bytes: {d[:64]!r}")
    except socket.timeout:
        print("  no reply within 2s (heuserver likely closes on bad tag — expected)")
    s.close()
except Exception as e:
    print(f"  connect/probe error: {e}")
PY

sleep 1
echo "=== heuserver still listening after the connection? (no crash) ==="
sudo ss -ltnp 2>/dev/null | grep 19093 && echo "  STILL LISTENING (served without crashing)" || echo "  *** died after connection ***"
echo "=== heuserver log (connection handling) ==="
grep -vE "cannot be preloaded" /tmp/heuserve.log | tail -10
echo "=== /etc guard ==="; A=$(sudo md5sum /etc/passwd | cut -d' ' -f1); [ "$A" = "$B" ] && echo "  SAFE" || echo "  *** CHANGED ***"
echo "=== cleanup ==="; sudo pkill -KILL -x FEXInterpreter 2>/dev/null; sudo kill -9 $NSPID 2>/dev/null; wait 2>/dev/null; echo done
