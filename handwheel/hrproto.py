#!/usr/bin/env python3
"""
hrproto.py - the HEIDENHAIN virtual-handwheel wire protocol (TCP 19035),
reverse-engineered from the *server* side (ipo.elf / HRSimServer.cpp) so a
native, cross-platform handwheel can drive the TNC 640 programming station
running under a hypervisor on UNIX/macOS (Option A).

This is the clean-room analogue of keypad/tnckeymap.py: it encodes/decodes the
on-the-wire frames; no HEIDENHAIN code or assets are used. The byte layout is
exact (from the decompiled `HrSimThread` @ 0x54adb0, `HrSim410GetInput`,
`HrSim520GetInput`, `HrSim*SetOutput`); see docs/18-handwheel-and-jhio-network.md.

Server endpoint: the NCK interpolator `ipo.elf` listens on TCP 19035 ("HEIDENHAIN
Handwheel simulation", per the guest portscan whitelist). It accepts up to 5
clients and exchanges one frame pair per PLC cycle.

OPEN (needs a live capture against a running control to finalise):
  - the connect handshake that seeds the per-connection `id` (frame field 0,
    which the server validates every frame),
  - the exact mapping of input words f1..f7/f8 to {jog-delta, axis, override1,
    override2, key-bitmap} (the getter semantics are known; the word order is not
    yet confirmed live).
The frame *structure* below is exact; field *labels* are best-effort from the
decompiled getters and are marked PROVISIONAL where not yet live-verified.
"""
import struct
from dataclasses import dataclass, field

PORT = 19035
INPUT_FRAME_LEN = 0x21   # 33 bytes: 8 x int32 LE + 1 byte  (read len in HrSimThread)

# ---------------------------------------------------------------------------
# Input frame  (handwheel -> ipo.elf), 33 bytes, little-endian (x86 guest).
#   off 0  int32  id        per-connection id; server rejects the frame unless
#                           it matches the value it holds for the slot.
#   off 4  int32  f1 )
#   off 8  int32  f2 )      handwheel state words. Among these are the signed
#   off12  int32  f3 )      jog-counter delta, the selected axis, and (HR520/550)
#   off16  int32  f4 )      two override values; HrSim*GetInput reduces a key
#   off20  int32  f5 )      table into a key bitmap. PROVISIONAL ordering.
#   off24  int32  f6 )
#   off28  int32  f7 )
#   off32  byte   f8        trailing status byte.
# ---------------------------------------------------------------------------
_IN = struct.Struct("<8i B")   # 8 signed int32 + 1 unsigned byte = 33 bytes
assert _IN.size == INPUT_FRAME_LEN


@dataclass
class InputFrame:
    id: int = 0
    f1: int = 0       # PROVISIONAL: jog-counter delta (signed)
    f2: int = 0       # PROVISIONAL: selected axis
    f3: int = 0       # PROVISIONAL: override 1 (feed/rapid)
    f4: int = 0       # PROVISIONAL: override 2 (spindle)
    f5: int = 0       # PROVISIONAL: key/button bitmap
    f6: int = 0
    f7: int = 0
    f8: int = 0       # status byte (0..255)

    def pack(self) -> bytes:
        return _IN.pack(self.id, self.f1, self.f2, self.f3,
                        self.f4, self.f5, self.f6, self.f7, self.f8 & 0xFF)

    @classmethod
    def unpack(cls, data: bytes) -> "InputFrame":
        if len(data) != INPUT_FRAME_LEN:
            raise ValueError(f"input frame must be {INPUT_FRAME_LEN} bytes, got {len(data)}")
        v = _IN.unpack(data)
        return cls(*v)


# ---------------------------------------------------------------------------
# Output frame  (ipo.elf -> handwheel), per PLC cycle:
#   - LED bitmap (one logical bit per panel LED; HrSim410/520SetOutput expand it
#     via ledMapHR510 / ledMapHR520).
#   - HR 520/550 display (HRDISPLAYDATA): 80 bytes of text = 4 lines x 20 chars,
#     a cursor (row + col*20), and an enable flag. HR 410 has LEDs only.
# The on-wire framing of the output block is the per-client connData slot
# (stride 0x8c). Decoded here as the logical content the SetOutput functions build.
# ---------------------------------------------------------------------------
HR_DISPLAY_LINES = 4
HR_DISPLAY_COLS = 20
HR_DISPLAY_LEN = HR_DISPLAY_LINES * HR_DISPLAY_COLS   # 80


@dataclass
class DisplayData:
    text: bytes = b" " * HR_DISPLAY_LEN     # 4x20
    cursor_row: int = 0
    cursor_col: int = 0
    enabled: bool = False

    def lines(self):
        t = self.text.ljust(HR_DISPLAY_LEN)[:HR_DISPLAY_LEN]
        return [t[i*HR_DISPLAY_COLS:(i+1)*HR_DISPLAY_COLS].decode("latin1")
                for i in range(HR_DISPLAY_LINES)]


@dataclass
class OutputFrame:
    leds: int = 0                  # LED bitmap
    display: DisplayData = field(default_factory=DisplayData)   # HR520/550 only


# Handwheel device types (HrSimCheckType / HRType in ipo.elf)
HR_410 = "HR410"    # simple: jog wheel + keys + LEDs (no display)
HR_520 = "HR520"    # portable, with LCD + two overrides
HR_550 = "HR550"    # wireless HR 520


def _selftest():
    f = InputFrame(id=0x1234, f1=-7, f2=2, f3=100, f4=50, f5=0xABCD, f6=0, f7=0, f8=0x80)
    b = f.pack()
    assert len(b) == 33, len(b)
    g = InputFrame.unpack(b)
    assert g == f, (g, f)
    # signed jog delta round-trips negative
    assert InputFrame.unpack(InputFrame(f1=-32768).pack()).f1 == -32768
    d = DisplayData(text=b"X+123.456  Y-7.890  " + b" " * 60, cursor_row=1, cursor_col=3, enabled=True)
    assert len(d.lines()) == 4 and len(d.lines()[0]) == 20
    print("hrproto self-test OK:")
    print(f"  input frame  = {INPUT_FRAME_LEN} bytes, struct {_IN.format!r}")
    print(f"  round-trip   = {f}")
    print(f"  display line = {d.lines()[0]!r}")


if __name__ == "__main__":
    _selftest()
