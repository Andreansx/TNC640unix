#!/usr/bin/env python3
"""
hr_probe.py - probe the live handwheel server (ipo.elf, TCP 19035) to validate the
33-byte frame and discover the connect/id handshake (the one item the static RE left
open). Read-mostly: it observes what the server sends on connect, then optionally
sends one input frame and watches the reply.

Usage:
  python3 hr_probe.py observe [host] [port]            # connect, print any server bytes
  python3 hr_probe.py send <id> [host] [port]          # send a frame with frame[0]=<id>, jog=+1
"""
import socket
import sys
import time

import hrproto as H


def observe(host, port, dwell=3.0):
    s = socket.create_connection((host, port), 5)
    s.settimeout(dwell)
    print(f"connected {host}:{port}; waiting {dwell}s for server-initiated bytes...")
    got = b""
    try:
        while True:
            d = s.recv(4096)
            if not d:
                print("server closed"); break
            got += d
            print(f"  <- {len(d)} bytes: {d[:64].hex()}")
    except socket.timeout:
        print(f"  (no more data; total {len(got)} bytes)")
    s.close()
    return got


def send(host, port, frame_id, jog=1):
    s = socket.create_connection((host, port), 5)
    s.settimeout(3)
    # see if server speaks first
    pre = b""
    try:
        pre = s.recv(4096)
        print(f"  pre-send server bytes: {len(pre)}: {pre[:48].hex()}")
    except socket.timeout:
        print("  (server silent before our frame)")
    f = H.InputFrame(id=frame_id, f1=jog, f2=0, f3=0, f4=0, f5=0, f6=0, f7=0, f8=0)
    s.sendall(f.pack())
    print(f"  -> sent 33-byte frame id={frame_id} jog={jog}")
    time.sleep(0.3)
    try:
        d = s.recv(4096)
        print(f"  <- reply {len(d)} bytes: {d[:64].hex()}")
    except socket.timeout:
        print("  (no reply within 3s)")
    # is the connection still alive? (server keeps accepted+valid clients)
    s.settimeout(2)
    try:
        s.sendall(f.pack()); alive = True
    except OSError:
        alive = False
    print(f"  connection still alive after frame: {alive}  (alive => id accepted)")
    s.close()


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else "observe"
    if cmd == "observe":
        host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
        port = int(sys.argv[3]) if len(sys.argv) > 3 else H.PORT
        observe(host, port)
    elif cmd == "send":
        fid = int(sys.argv[2], 0)
        host = sys.argv[3] if len(sys.argv) > 3 else "127.0.0.1"
        port = int(sys.argv[4]) if len(sys.argv) > 4 else H.PORT
        send(host, port, fid)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
