#!/usr/bin/env python3
"""
jhioproto.py - the HEIDENHAIN JHIO PLC-I/O "network" wire protocol (TCP 19009),
reverse-engineered from the guest's own libjhiosimnet.so (linked by plc.elf) so a
cross-platform host I/O-simulation can drive the TNC 640 guest's machine I/O on
UNIX/macOS (Option A) - WITHOUT the Windows-only VBoxJHIO HGCM extension pack.

This is the clean-room analogue of handwheel/hrproto.py and keypad/tnckeymap.py.
Byte layout from the decompiled libjhiosimnet (work/re/out/libjhiosimnet.decomp.c:
print_JHIO_HEADER field offsets, the guest-connection thread, djb_hash); see
docs/18-handwheel-and-jhio-network.md.

Topology (default): the GUEST is the TCP server. applaunch sets
  JHIOSIM_GUEST_IF=<ethN>  JHIOSIM_SVR_PORT=19009
so plc.elf binds <machine-net iface>:19009. A host I/O-sim connects as CLIENT:
  JHIOSIM_MODE=1  JHIOSIM_SVR_IP=<guest ip>  JHIOSIM_SVR_PORT=19009
(libjhiosimnet supports either role via JHIOSIM_MODE: 0=server, 1=client.)

Per PLC scan cycle: send the 740-byte JHIO_HEADER, exchange the I/O data block
(lDataSize bytes at lDataOffset; PutBlocks diffs with a djb2 change-hash), receive
the host's input image, and keep lockstep via SignalPlcCycleDone/WaitForSimCycleDone.
"""
import socket
import struct

PORT = 19009
HEADER_LEN = 0x2e4            # 740 bytes (read length in the exchange loop)

# Environment variables libjhiosimnet reads (getenv).
ENV_MODE = "JHIOSIM_MODE"        # "0"=server, "1"=client
ENV_GUEST_IF = "JHIOSIM_GUEST_IF"  # server bind interface (e.g. eth0)
ENV_SVR_IP = "JHIOSIM_SVR_IP"      # client: server IP to connect to
ENV_SVR_PORT = "JHIOSIM_SVR_PORT"  # TCP port, default 19009

# JHIO_HEADER field byte-offsets (from print_JHIO_HEADER; struct is a long[] so
# idx i -> byte i*4). All int32 little-endian (x86 guest). start/len are byte
# ranges into the I/O data block; *_ADC/_DAC are analog channels per terminal.
HDR = {
    "lInitialized":        0x000,
    "lPLCRunning":         0x004,
    "lVersion":            0x008,   # 100..400 = schema v1.0..4.0
    # PLConfig.PLLogicNr[] at 0x010..; szMapFileName char buffer follows (<0x120)
    "lStartInputs":        0x120, "lLenInputs":       0x124,
    "lStartOutputs":       0x128, "lLenOutputs":      0x12c,
    "lStartInputBWDs":     0x130, "lLenInputBWDs":    0x134,
    "lStartOutputBWDs":    0x138, "lLenOutputBWDs":   0x13c,
    "lStartX45_ADC":       0x140, "lLenX45_ADC":      0x144,
    "lStartX48_ADC":       0x148, "lLenX48_ADC":      0x14c,
    "lStartX8_9_DAC":      0x150, "lLenX8_9_DAC":     0x154,
    "lStartX148_ADC":      0x158, "lLenX148_ADC":     0x15c,
    "lStartPL410_ADC":     0x160, "lLenPL410_ADC":    0x164,
    "lStartPL510_ADC":     0x168, "lLenPL510_ADC":    0x16c,
    "lStartX150":          0x178, "lLenX150":         0x17c,
    "lStartX151":          0x180, "lLenX151":         0x184,
    "lSimulationId":       0x188,
    "lvirtualTNCLicense":  0x18c,
    "lControlIsReady":     0x190,   # "control voltage on / machine ready" flag
    "lSynchronousOperation": 0x194,
    "lStartX12":           0x198, "lLenX12":          0x19c,
    "lStartX13":           0x1a0, "lLenX13":          0x1a4,
    "lHSCIConfiguration":  0x1a8,
    "lStartMOP_ADC":       0x1ac, "lLenMOP_ADC":      0x1b0,
    "lStartES":            0x1b4, "lLenES":           0x1b8,   # emergency-stop chain
    "lDataOffset":         0x1bc, "lDataSize":        0x1c0,
    # szSPLCMapFileName char buffer at 0x1c4.. (256 B, up to 0x2c4)
    "lStartSPLCInputs":    0x2c4, "lLenSPLCInputs":   0x2c8,   # safety PLC
    "lStartSPLCOutputs":   0x2cc, "lLenSPLCOutputs":  0x2d0,
}


# --- RPC framing (corrected against the LIVE control on yeen, 2026-06-22) -----------
# JHIO is NOT a passive "header pushed on connect": the connected server waits for a
# request. It is a bidirectional RPC (send_request / read_response / fcn_id_to_str).
#   REQUEST  = 20 bytes (0x14): [+4] cFcnId(u8), [+8] parm1, [+0xc] parm2, [+0x10] parm3
#   RESPONSE = 16 bytes (0x10): [+4] cFcnId(u8), [+8] rc,    [+0xc] val
#   cFcnId opcodes = 10..26 (0x0a..0x1a), one per _JHIOIntern* call. The exact
#   opcode<->name map needs the fcn_id_to_str jump-table disassembly (.data 0x1ad2c).
# Bulk transfers (the 740-byte JHIO_HEADER, the lDataSize block) follow the relevant
# GetHeader/GetBlock/PutBlock replies. See docs/18 §1.3.
RPC_REQ_LEN = 0x14
RPC_RSP_LEN = 0x10
CFCNID_MIN, CFCNID_MAX = 0x0a, 0x1a


def pack_request(cfcnid: int, parm1=0, parm2=0, parm3=0) -> bytes:
    b = bytearray(RPC_REQ_LEN)
    b[4] = cfcnid & 0xFF
    b[8:12] = (parm1 & 0xFFFFFFFF).to_bytes(4, "little")
    b[12:16] = (parm2 & 0xFFFFFFFF).to_bytes(4, "little")
    b[16:20] = (parm3 & 0xFFFFFFFF).to_bytes(4, "little")
    return bytes(b)


def unpack_response(b: bytes):
    if len(b) != RPC_RSP_LEN:
        raise ValueError(f"response must be {RPC_RSP_LEN} bytes, got {len(b)}")
    return {"cFcnId": b[4],
            "rc": int.from_bytes(b[8:12], "little"),
            "val": int.from_bytes(b[12:16], "little")}


def djb2(data: bytes) -> int:
    """The exact hash libjhiosimnet uses ('JHIO_HEADER hash=0x%x'): djb2, 32-bit wrap."""
    h = 0x1505
    for b in data:
        h = (h * 0x21 + b) & 0xFFFFFFFF
    return h


class JhioHeader:
    """A 740-byte JHIO_HEADER with named int32 accessors at the decoded offsets."""
    def __init__(self, raw: bytes = None):
        self.raw = bytearray(raw if raw is not None else b"\x00" * HEADER_LEN)
        if len(self.raw) != HEADER_LEN:
            raise ValueError(f"JHIO_HEADER must be {HEADER_LEN} bytes, got {len(self.raw)}")

    def get(self, name: str) -> int:
        return struct.unpack_from("<i", self.raw, HDR[name])[0]

    def set(self, name: str, val: int) -> "JhioHeader":
        struct.pack_into("<i", self.raw, HDR[name], val)
        return self

    def hash(self) -> int:
        return djb2(bytes(self.raw))

    def __bytes__(self):
        return bytes(self.raw)


class JhioClient:
    """Minimal host-side I/O-sim CLIENT skeleton (connect to guest:19009).

    NOTE (live finding): the guest server does NOT push the header on connect — it is an
    RPC (see pack_request/unpack_response). recv_header() below is only valid AFTER issuing
    the GetHeader RPC; a passive recv on the live guest returns nothing. The exact GetHeader
    opcode (within cFcnId 10..26) is the remaining item for a working client.
    """
    def __init__(self, host: str, port: int = PORT, timeout: float = 5.0):
        self.host, self.port, self.timeout = host, port, timeout
        self.sock = None

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), self.timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return self

    def recv_header(self) -> JhioHeader:
        buf = b""
        while len(buf) < HEADER_LEN:
            chunk = self.sock.recv(HEADER_LEN - len(buf))
            if not chunk:
                raise ConnectionError("peer closed during JHIO_HEADER")
            buf += chunk
        return JhioHeader(buf)

    def send_header(self, hdr: JhioHeader):
        self.sock.sendall(bytes(hdr))

    def close(self):
        if self.sock:
            self.sock.close()
            self.sock = None


def _selftest():
    # djb2 known-answer: empty -> 5381; "JHIO" stable
    assert djb2(b"") == 0x1505
    assert djb2(b"JHIO") == ((((0x1505*33 + ord('J'))*33 + ord('H'))*33 + ord('I'))*33 + ord('O')) & 0xFFFFFFFF
    h = JhioHeader()
    h.set("lVersion", 400).set("lControlIsReady", 1).set("lDataOffset", 0x2e4).set("lDataSize", 0x800)
    assert len(bytes(h)) == HEADER_LEN
    assert h.get("lVersion") == 400 and h.get("lControlIsReady") == 1
    # offsets are unique and in range
    offs = sorted(HDR.values())
    assert offs == sorted(set(offs)) and offs[-1] + 4 <= HEADER_LEN
    print("jhioproto self-test OK:")
    print(f"  JHIO_HEADER = {HEADER_LEN} bytes, {len(HDR)} named int32 fields")
    print(f"  version=400 control_ready=1 -> djb2 header hash = 0x{h.hash():08x}")
    print(f"  config: {ENV_MODE}/{ENV_GUEST_IF}/{ENV_SVR_IP}/{ENV_SVR_PORT} (default port {PORT})")


if __name__ == "__main__":
    _selftest()
