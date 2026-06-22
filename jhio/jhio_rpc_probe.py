#!/usr/bin/env python3
"""
jhio_rpc_probe.py - drive the live JHIO RPC server (guest plc.elf, TCP 19009) using the
recovered opcode map, READ-ONLY ops only, to validate the protocol + read the real
machine-I/O map. Safe: issues only INIT + GET_* (no writes / no cycle-blocking ops).

Self-retrying: with IOSIM/Network=on but no host I/O-sim peer, the control shuts down
~3 min after boot, so this loops connecting until the JHIO server answers (or a deadline).

Usage: python3 jhio_rpc_probe.py [host] [port] [deadline_s]
"""
import socket
import sys
import time
import jhioproto as J


def rpc(s, op, p1=0, p2=0, p3=0, greedy=800, dwell=2.0):
    s.sendall(J.pack_request(op, p1, p2, p3))
    s.settimeout(dwell)
    buf = b""
    try:
        while len(buf) < greedy:
            c = s.recv(greedy - len(buf))
            if not c:
                break
            buf += c
    except socket.timeout:
        pass
    return buf


def one_session(host, port):
    s = socket.create_connection((host, port), 3)
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    out = []

    def show(op, buf):
        name = J.OPCODE_NAME.get(op, hex(op))
        if len(buf) >= J.RPC_RSP_LEN:
            r = J.unpack_response(buf[:J.RPC_RSP_LEN])
            bulk = buf[J.RPC_RSP_LEN:]
            out.append(f"  {name:20s} -> cFcnId={r['cFcnId']} rc={r['rc']} val={r['val']}"
                       + (f" +{len(bulk)}B bulk" if bulk else ""))
            return r, bulk
        out.append(f"  {name:20s} -> {len(buf)}B {buf[:24].hex()}")
        return None, buf

    show(J.INTERN_INIT, rpc(s, J.INTERN_INIT))
    show(J.IS_SIM_RUNNING, rpc(s, J.IS_SIM_RUNNING))
    show(J.GET_SIM_ID, rpc(s, J.GET_SIM_ID))
    show(J.GET_HEADERSIZE, rpc(s, J.GET_HEADERSIZE))
    show(J.GET_DATASIZE, rpc(s, J.GET_DATASIZE))
    show(J.GET_BASE_OFFSET, rpc(s, J.GET_BASE_OFFSET))
    buf = rpc(s, J.GET_HEADER, greedy=16 + J.HEADER_LEN + 32)
    r, bulk = show(J.GET_HEADER, buf)
    s.close()

    hdr = bulk if len(bulk) >= 0x1c4 else (buf if len(buf) >= 0x1c4 else b"")
    if len(hdr) >= 0x1c4:
        h = J.JhioHeader(hdr[:J.HEADER_LEN].ljust(J.HEADER_LEN, b"\x00"))
        out.append("  === LIVE JHIO_HEADER ===")
        for k in ["lInitialized", "lPLCRunning", "lVersion", "lControlIsReady",
                  "lvirtualTNCLicense", "lSimulationId", "lDataOffset", "lDataSize"]:
            out.append(f"    {k:22s} = {h.get(k)}")
        for rgn in ["Inputs", "Outputs", "InputBWDs", "OutputBWDs", "SPLCInputs", "SPLCOutputs",
                    "PL410_ADC", "ES", "MOP_ADC"]:
            st, ln = h.get(f"lStart{rgn}"), h.get(f"lLen{rgn}")
            if ln:
                out.append(f"    region {rgn:12s} start={st:<8} len={ln}")
        out.append(f"    DECODE {'VALIDATED' if 100 <= h.get('lVersion') <= 1000 else 'SUSPECT'}")
    # success = we got at least one well-formed response
    ok = any("rc=" in l for l in out)
    return ok, "\n".join(out)


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else J.PORT
    deadline = time.monotonic() + (float(sys.argv[3]) if len(sys.argv) > 3 else 240.0)
    n = 0
    while time.monotonic() < deadline:
        n += 1
        try:
            ok, text = one_session(host, port)
            if ok:
                print(f"[try {n}] JHIO RPC ANSWERED:")
                print(text)
                return 0
        except (ConnectionError, OSError):
            pass
        time.sleep(1.5)
    print(f"deadline reached after {n} tries; JHIO server never answered an RPC")
    return 1


if __name__ == "__main__":
    sys.exit(main())
