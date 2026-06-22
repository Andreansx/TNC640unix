# jhio/ — cross-platform PLC-I/O simulation for the TNC 640 (network transport)

The third piece of the host control suite (after [`../keypad/`](../keypad/) and
[`../handwheel/`](../handwheel/)). It speaks the **JHIO network protocol** the guest serves on
**TCP 19009** (`libjhiosimnet.so`, linked by `plc.elf`) — the **cross-platform** alternative to
the Windows-only `VBoxJHIO` HGCM extension pack. This is what reframes the project's documented
"deepest blocker": the PLC-I/O sim is a plain TCP protocol, not a Windows lock-in.

Full RE: **[../docs/18-handwheel-and-jhio-network.md](../docs/18-handwheel-and-jhio-network.md)** §1.

## Status

- **`jhioproto.py`** — the wire-protocol core (testable; analogue of `handwheel/hrproto.py`):
  - the **740-byte `JHIO_HEADER`** with all 46 decoded `int32` fields (the machine-I/O map:
    Inputs/Outputs/BWDs, per-terminal ADC/DAC, ES, SPLC, `lControlIsReady`, `lDataOffset/Size`, …);
  - the exact **djb2** header hash the lib uses;
  - the env-var config (`JHIOSIM_MODE/GUEST_IF/SVR_IP/SVR_PORT`);
  - a **client skeleton** (`JhioClient`) that connects to `guest:19009` and transfers the header.
  - `python3 jhioproto.py` runs a self-test (header round-trip, djb2 known-answer, offset sanity).
- **Per-cycle block exchange + a machine I/O model** — TODO. The header + handshake are decoded;
  the `PutBlocks` diff framing of the I/O data block is best finalised against a **live guest**,
  and a usable host needs a machine I/O *model* (which input signals to drive — the role of the
  Windows `iosim.dll`/`plcmap.dll`). Whether *demo programming + simulation* needs more than a
  minimal "control-ready, no faults" model is the open empirical question (see doc 08 §JHIO).

## How to use against a live control

Guest side (default, set by `applaunch`): `plc.elf` binds `<machine-net eth>:19009` as the server.
Host side: run a client with `JHIOSIM_MODE=1`, `JHIOSIM_SVR_IP=<guest ip>`, `JHIOSIM_SVR_PORT=19009`.

```bash
python3 jhioproto.py            # codec self-test
# then: JhioClient(guest_ip).connect().recv_header()  -> inspect the live machine-I/O map
```

No HEIDENHAIN code or assets are bundled; this is a clean-room protocol implementation.
