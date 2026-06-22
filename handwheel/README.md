# handwheel/ — native virtual handwheel for the TNC 640 programming station

Clean-room, cross-platform replacement for the Windows `handwheel.exe`, the second piece of the
host control suite (after [`../keypad/`](../keypad/)). It speaks the handwheel protocol the guest
NCK (`ipo.elf`) serves on **TCP 19035** ("HEIDENHAIN Handwheel simulation").

Full protocol RE: **[../docs/18-handwheel-and-jhio-network.md](../docs/18-handwheel-and-jhio-network.md)**.

## Status

- **`hrproto.py`** — the wire-protocol codec (the durable, testable core; analogue of
  `keypad/tnckeymap.py`). Encodes/decodes the **33-byte input frame** (handwheel → NCK: 8×int32
  LE + 1 byte) and the **output frame** (NCK → handwheel: LED bitmap + HR 520/550 `HRDISPLAYDATA`
  = 4×20 display + cursor + enable). Frame *structure* is exact (decompiled from `HrSimThread`,
  `HrSim410/520GetInput`, `HrSim*SetOutput`). `python3 hrproto.py` runs a self-test.
- **GUI + live transport** — TODO. Two items must be pinned with a **live packet capture** against
  a running control before a GUI can be faithful:
  1. the **connect handshake** that seeds the per-connection `id` (frame field 0, validated every
     frame by the server);
  2. the exact assignment of input words `f1..f8` to {jog delta, selected axis, override 1,
     override 2, key bitmap} — the getter *semantics* are known (HR 410 = jog + keys; HR 520/550 =
     jog + 2 overrides + wider key bitmap), the word *order* is provisional.

## Why server-authoritative

The Windows client serialises with Qt `QDataStream` over `QTcpSocket`; the **server's read order**
in `ipo.elf` is the ground truth, which is what `hrproto.py` encodes. Up to **5** handwheels may
connect; **HR 410** (jog + keys + LEDs, no screen) and **HR 520/550** (with LCD + two overrides)
are both supported by the same server.

## Validating against a live control

On the x86-64 Linux host running the guest (see `../docs/11-running-on-linux.md`):

```bash
# the guest exposes 19035 on its host-only/NAT address; with the real handwheel.exe on a Windows
# reference, capture the exchange:  tcpdump -i <if> -w hr.pcap 'tcp port 19035'
# then compare the 33-byte client frames to hrproto.InputFrame to fix f1..f8 and the id handshake.
python3 hrproto.py     # self-test of the codec
```

No HEIDENHAIN assets are bundled; the on-screen UI is built from scratch (as in `keypad/`).
