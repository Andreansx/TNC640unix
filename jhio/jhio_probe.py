#!/usr/bin/env python3
"""
jhio_probe.py - connect to the guest JHIO server (TCP 19009) and validate the decoded
JHIO_HEADER against the LIVE control. The guest (plc.elf) is the server and sends the
740-byte JHIO_HEADER on connect, so this is a non-destructive read-only probe.

Usage: python3 jhio_probe.py [host] [port]   (default 127.0.0.1 19009)
"""
import socket
import sys

import jhioproto as J


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else J.PORT
    s = socket.create_connection((host, port), 5)
    s.settimeout(5)
    buf = b""
    try:
        while len(buf) < J.HEADER_LEN:
            chunk = s.recv(J.HEADER_LEN - len(buf))
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    finally:
        s.close()

    print(f"connected {host}:{port}, received {len(buf)} bytes (expect {J.HEADER_LEN})")
    if len(buf) < 0x1c4:
        print("  -> short read; guest not serving a full header. (network IOSIM enabled?)")
        if buf:
            print("  first bytes:", buf[:32].hex())
        return 1

    h = J.JhioHeader(buf.ljust(J.HEADER_LEN, b"\x00"))
    print(f"  djb2(header) = 0x{h.hash():08x}")
    # the fields that prove a correct decode + the live machine state
    key = ["lInitialized", "lPLCRunning", "lVersion", "lControlIsReady",
           "lvirtualTNCLicense", "lSimulationId", "lHSCIConfiguration",
           "lSynchronousOperation", "lDataOffset", "lDataSize"]
    print("  --- header scalars ---")
    for k in key:
        print(f"    {k:24s} = {h.get(k)}")
    print("  --- I/O regions (start, len) ---")
    regions = ["Inputs", "Outputs", "InputBWDs", "OutputBWDs", "X45_ADC", "X48_ADC",
               "X8_9_DAC", "X148_ADC", "PL410_ADC", "PL510_ADC", "X150", "X151",
               "X12", "X13", "MOP_ADC", "ES", "SPLCInputs", "SPLCOutputs"]
    total = 0
    for r in regions:
        st, ln = h.get(f"lStart{r}"), h.get(f"lLen{r}")
        if ln:
            print(f"    {r:14s} start={st:<8} len={ln}")
            total += ln
    print(f"  total mapped I/O bytes (sum of lens) = {total}; header lDataSize = {h.get('lDataSize')}")
    ver = h.get("lVersion")
    ok = (1 <= ver <= 1000) and h.get("lDataSize") > 0
    print(f"  DECODE {'VALIDATED' if ok else 'SUSPECT'} (version={ver}, dataSize={h.get('lDataSize')})")
    return 0 if ok else 2


if __name__ == "__main__":
    sys.exit(main())
