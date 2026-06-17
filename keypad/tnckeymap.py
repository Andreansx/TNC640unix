#!/usr/bin/env python3
"""
tnckeymap.py - Operation -> keycode mapping for a native TNC 640 keypad.

Clean-room reconstruction of how HEIDENHAIN's keypad.exe turns each on-screen
button (its `operation` string, recovered from the keypad QML) into a keypress
delivered to the HeROS5 guest. No HEIDENHAIN code or assets are used here; this
is derived from:

  * the keypad QML layout (button -> `operation` string),
  * the guest keymap  keymap_te530_*_vbox.xml  (X-keycode + modifier -> virtualKey),
  * the documented relation  X-keycode = Linux/evdev keycode + 8,
  * Linux keycode -> AT scancode-set-1 (the codes VirtualBox's putScancodes wants),
  * live validation against the running control (CE, soft-key 1 confirmed).

Two transports are produced from the same table:
  * scancodes_for(op) -> AT set-1 make/break bytes for
        VBoxManage controlvm <vm> keyboardputscancode ...
    (this is what the original keypad.exe does for a VirtualBox VM: IKeyboard::putScancodes)
  * heuinput_tokens(op) -> "KP <n>\nKR <n>\n" Linux keycodes for the guest
        /tmp/__heuinput FIFO (the alternative transport).

See docs/12-keypad-keymap.md for the full write-up.
"""

# ---------------------------------------------------------------------------
# Modifier keys (Linux/evdev keycodes)
# ---------------------------------------------------------------------------
MOD_CTRL = 29      # KEY_LEFTCTRL
MOD_ALT = 56       # KEY_LEFTALT
MOD_SHIFT = 42     # KEY_LEFTSHIFT

CA = (MOD_CTRL, MOD_ALT)            # Ctrl+Alt  - TNC popups / modes / programming keys
SCA = (MOD_SHIFT, MOD_CTRL, MOD_ALT)  # Shift+Ctrl+Alt - vertical soft keys
NONE = ()

# ---------------------------------------------------------------------------
# Linux/evdev keycode -> AT scancode set 1.
#   value = make byte (break = make | 0x80)
#   extended keys are prefixed with 0xE0 (break = 0xE0, make|0x80)
# Only the keycodes this keypad emits are listed.
# ---------------------------------------------------------------------------
_SET1 = {
    1: 0x01,                                   # ESC
    2: 0x02, 3: 0x03, 4: 0x04, 5: 0x05, 6: 0x06,   # 1 2 3 4 5
    7: 0x07, 8: 0x08, 9: 0x09, 10: 0x0A, 11: 0x0B, # 6 7 8 9 0
    14: 0x0E,                                  # BACKSPACE
    29: 0x1D, 42: 0x2A, 56: 0x38,              # LCTRL LSHIFT LALT (modifiers)
    16: 0x10, 17: 0x11, 18: 0x12, 19: 0x13, 20: 0x14,  # Q W E R T
    21: 0x15, 22: 0x16, 23: 0x17, 24: 0x18, 25: 0x19,  # Y U I O P
    28: 0x1C,                                  # ENTER
    30: 0x1E, 31: 0x1F, 32: 0x20, 33: 0x21, 34: 0x22,  # A S D F G
    35: 0x23, 36: 0x24, 37: 0x25, 38: 0x26,    # H J K L
    44: 0x2C, 45: 0x2D, 46: 0x2E, 47: 0x2F, 48: 0x30, # Z X C V B
    49: 0x31, 50: 0x32,                        # N M
    55: 0x37,                                  # KP_ASTERISK
    59: 0x3B, 60: 0x3C, 61: 0x3D, 62: 0x3E, 63: 0x3F, # F1..F5
    64: 0x40, 65: 0x41, 66: 0x42, 67: 0x43, 68: 0x44, # F6..F10
    71: 0x47, 72: 0x48, 73: 0x49, 74: 0x4A,    # KP7 KP8 KP9 KP-
    75: 0x4B, 76: 0x4C, 77: 0x4D, 78: 0x4E,    # KP4 KP5 KP6 KP+
    79: 0x4F, 80: 0x50, 81: 0x51, 82: 0x52, 83: 0x53, # KP1 KP2 KP3 KP0 KP.
    87: 0x57, 88: 0x58,                        # F11 F12
}
# Extended (0xE0-prefixed) keys
_SET1_EXT = {
    96: 0x1C,    # KP_ENTER
    98: 0x35,    # KP_SLASH
    102: 0x47,   # HOME
    103: 0x48,   # UP
    104: 0x49,   # PAGEUP
    105: 0x4B,   # LEFT
    106: 0x4D,   # RIGHT
    107: 0x4F,   # END
    108: 0x50,   # DOWN
    109: 0x51,   # PAGEDOWN
    110: 0x52,   # INSERT
    111: 0x53,   # DELETE
}


def _make_break(keycode):
    """Return (make_bytes, break_bytes) AT set-1 sequences for one Linux keycode."""
    if keycode in _SET1_EXT:
        b = _SET1_EXT[keycode]
        return [0xE0, b], [0xE0, b | 0x80]
    if keycode in _SET1:
        b = _SET1[keycode]
        return [b], [b | 0x80]
    raise KeyError(f"no AT set-1 scancode for Linux keycode {keycode}")


# ---------------------------------------------------------------------------
# The keypad: operation string -> Key
#   label : short text drawn on the button (we do NOT ship HEIDENHAIN icons)
#   kc    : Linux/evdev keycode, or None if intentionally unmapped
#   mods  : tuple of modifier keycodes held while kc is pressed
#   note  : optional remark (esp. for the few keys still being confirmed live)
#
# How kc was derived for the special TNC keys:
#   guest keymap gives  X-keycode (+modifier) -> virtualKey ; kc = X-keycode - 8.
#   e.g. PGM_MGT: X 0x21 (Ctrl+Alt) -> 0x21-8 = 0x19 = 25 = KEY_P, with Ctrl+Alt.
# Standard keys (digits, ENT, cursor, BS) are NOT remapped by the guest and use
# their natural keycodes.
# ---------------------------------------------------------------------------
class Key:
    __slots__ = ("op", "label", "kc", "mods", "note")

    def __init__(self, op, label, kc, mods=NONE, note=""):
        self.op = op
        self.label = label
        self.kc = kc
        self.mods = tuple(mods)
        self.note = note

    @property
    def mapped(self):
        return self.kc is not None


# operation -> Key.  Operation strings are exactly those in the keypad QML (qml_08,
# "NC Steuerungsbedienfeld horizontal").
KEYS = {
    # --- grid1: screen/softkey nav, popups, operating modes ---------------
    "F11":      Key("F11", "LAYOUT\n3", 87, NONE, "F11 -> SCREEN_MANAGEMENT (3-split)"),
    "F9":       Key("F9", "SK◄", 67, NONE, "F9 -> SOFTKEYSCROLLLEFT"),
    "VSKDOWN":  Key("VSKDOWN", "VSK▼", None, NONE, "vertical soft-key scroll down - no keymap entry; confirm live"),
    "F10":      Key("F10", "SK►", 68, NONE, "F10 -> SOFTKEYSCROLLRIGHT"),
    "F12":      Key("F12", "LAYOUT\n2", 88, NONE, "F12 -> SCREEN_CHANGE (2-split)"),

    "PGMMGT":   Key("PGMMGT", "PGM\nMGT", 25, CA),
    "CALC":     Key("CALC", "CALC", 49, CA),
    "MOD":      Key("MOD", "MOD", 50, CA),
    "HELP":     Key("HELP", "HELP", 35, CA),
    "ERR":      Key("ERR", "ERR", 38, CA),

    "MANUAL":   Key("MANUAL", "MANUAL\nOP.", 2, CA),
    "HANDW":    Key("HANDW", "EL.\nHANDW", 3, CA),
    "SMART":    Key("SMART", "smart\nNC", 4, CA),
    "EDIT":     Key("EDIT", "PGM\nEDIT", 5, CA),
    "TI":       Key("TI", "MDI", 16, CA, "manual data input (positioning w/ MDI)"),

    "SINGLE":   Key("SINGLE", "SINGLE\nBLOCK", 17, CA),
    "AUTO":     Key("AUTO", "PGM\nRUN", 18, CA, "full-sequence run"),
    "SIMU":     Key("SIMU", "TEST\nRUN", 19, CA),

    # --- grid2: contour/path, cycles, tools, program ----------------------
    "APRDEP":   Key("APRDEP", "APPR\nDEP", 6, CA),
    "FK":       Key("FK", "FK", 7, CA),
    "M":        Key("M", "M", 50, NONE, "plain letter M (M-functions); confirm live"),
    "CHF":      Key("CHF", "CHF°", 9, CA),
    "LINE":     Key("LINE", "L", 10, CA),

    "CR":       Key("CR", "CR", 20, CA),
    "RND":      Key("RND", "RND", 21, CA),
    "CT":       Key("CT", "CT", 22, CA),
    "CC":       Key("CC", "CC", 23, CA),
    "C":        Key("C", "C", 24, CA),

    "TPR":      Key("TPR", "TOUCH\nPROBE", 30, CA),
    "CYCLDEF":  Key("CYCLDEF", "CYCL\nDEF", 31, CA),
    "CYCLCALL": Key("CYCLCALL", "CYCL\nCALL", 32, CA),
    "LBLSET":   Key("LBLSET", "LBL\nSET", 33, CA),
    "LBLCALL":  Key("LBLCALL", "LBL\nCALL", 34, CA),

    "STOP":     Key("STOP", "STOP", 44, CA),
    "TOOLDEF":  Key("TOOLDEF", "TOOL\nDEF", 45, CA),
    "TOOLCALL": Key("TOOLCALL", "TOOL\nCALL", 46, CA),
    "SPEC-FCT": Key("SPEC-FCT", "SPEC\nFCT", 47, CA),
    "PGMCALL":  Key("PGMCALL", "PGM\nCALL", 48, CA),

    # --- grid3: axes + numeric entry --------------------------------------
    "X":   Key("X", "X", 71, NONE),
    "7":   Key("7", "7", 8, NONE),
    "8":   Key("8", "8", 9, NONE),
    "9":   Key("9", "9", 10, NONE),
    "Y":   Key("Y", "Y", 75, NONE),
    "4":   Key("4", "4", 5, NONE),
    "5":   Key("5", "5", 6, NONE),
    "6":   Key("6", "6", 7, NONE),
    "Z":   Key("Z", "Z", 79, NONE),
    "1":   Key("1", "1", 2, NONE),
    "2":   Key("2", "2", 3, NONE),
    "3":   Key("3", "3", 4, NONE),
    "IV":  Key("IV", "IV", 72, NONE, "4th axis"),
    "0":   Key("0", "0", 11, NONE),
    ",":   Key(",", ".", 55, NONE, "decimal point (KEY_TNC_DECIMAL)"),
    "+/-": Key("+/-", "+/−", 74, NONE),
    "V":   Key("V", "V", 80, NONE, "5th axis"),
    "BS":  Key("BS", "⌫", 14, NONE, "backspace"),
    "ACTPOS": Key("ACTPOS", "ACTL\nPOS", 98, NONE, "actual-position capture"),
    "Q":   Key("Q", "Q", 77, NONE, "Q parameter"),

    # --- grid4: navigation / editing cluster ------------------------------
    "NB_PGDN":  Key("NB_PGDN", "MENU", 109, (MOD_CTRL,), "smartNC menu select (SMARTNC_PGDOWN = Ctrl+PgDn)"),
    "HOME":     Key("HOME", "⌂", 102, NONE, "home / begin"),
    "CURUP":    Key("CURUP", "▲", 103, NONE),
    "PGUP":     Key("PGUP", "PG▲", 104, NONE),
    "FRMUP":    Key("FRMUP", "FORM▲", 103, CA, "smartNC form up (SMARTNC_UP = Ctrl+Alt+Up)"),
    "CURLEFT":  Key("CURLEFT", "◄", 105, NONE),
    "GOTO":     Key("GOTO", "GOTO", 76, NONE),
    "CURRIGHT": Key("CURRIGHT", "►", 106, NONE),
    "FRMDOWN":  Key("FRMDOWN", "FORM▼", 108, CA, "smartNC form down (SMARTNC_DOWN = Ctrl+Alt+Down)"),
    "CEND":     Key("CEND", "END", 107, NONE, "cursor End (shares keycode with block END)"),
    "CURDOWN":  Key("CURDOWN", "▼", 108, NONE),
    "PGDN":     Key("PGDN", "PG▼", 109, NONE),
    "CE":       Key("CE", "CE", 83, NONE, "clear entry (VALIDATED live)"),
    "ZDEL":     Key("ZDEL", "DEL", 111, NONE, "delete block (KEY_TNC_DEL_BLOCK)"),
    "POLAR":    Key("POLAR", "P", 73, NONE, "polar coords"),
    "INC":      Key("INC", "I", 81, NONE, "incremental"),

    # --- entry row: NO ENT, ENT (double width), END -----------------------
    "NO\nENT":  Key("NO\nENT", "NO\nENT", 78, NONE),
    "ENT":      Key("ENT", "ENT", 28, NONE),
    "ZEND":     Key("ZEND", "END", 107, NONE, "end block (KEY_TNC_END_BLOCK)"),

    # blanks
    "leer":     Key("leer", "", None, NONE, "blank filler key"),
}


def scancodes_for(op):
    """AT set-1 make/break byte list for one operation, ready for putScancodes.

    Order: press modifiers, press key, release key, release modifiers."""
    k = KEYS.get(op)
    if k is None or k.kc is None:
        return None
    out = []
    mk, brk = _make_break(k.kc)
    mod_seq = [_make_break(m) for m in k.mods]
    for m_make, _ in mod_seq:
        out += m_make
    out += mk
    out += brk
    for _, m_break in reversed(mod_seq):
        out += m_break
    return out


def scancode_hex(op):
    """Space-separated 2-digit hex string for `VBoxManage ... keyboardputscancode`."""
    codes = scancodes_for(op)
    if codes is None:
        return None
    return " ".join(f"{b:02x}" for b in codes)


def heuinput_tokens(op):
    """`KP/KR` Linux-keycode token string for the guest /tmp/__heuinput FIFO."""
    k = KEYS.get(op)
    if k is None or k.kc is None:
        return None
    presses = list(k.mods) + [k.kc]
    releases = list(reversed(presses))
    lines = [f"KP {c}" for c in presses] + [f"KR {c}" for c in releases]
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# Self-test / table dump
# ---------------------------------------------------------------------------
if __name__ == "__main__":
    import sys

    # Sanity checks against live-validated / documented values.
    assert scancodes_for("CE") == [0x53, 0xD3], scancodes_for("CE")
    assert scancode_hex("CE") == "53 d3"
    assert heuinput_tokens("CE") == "KP 83\nKR 83\n"
    assert heuinput_tokens("PGMMGT") == "KP 29\nKP 56\nKP 25\nKR 25\nKR 56\nKR 29\n"
    assert scancode_hex("PGMMGT") == "1d 38 19 99 b8 9d"
    assert heuinput_tokens("SIMU") == "KP 29\nKP 56\nKP 19\nKR 19\nKR 56\nKR 29\n"  # TEST
    assert scancode_hex("F9") == "43 c3"      # soft-key scroll left = F9
    assert scancode_hex("ACTPOS") == "e0 35 e0 b5"  # extended KP_SLASH
    print("self-test OK\n")

    width = max(len(op) for op in KEYS)
    print(f"{'operation':<{width}}  {'label':<12} {'kc':>4} {'mods':<14} {'scancodes':<18} note")
    print("-" * 100)
    for op, k in KEYS.items():
        lbl = k.label.replace("\n", "/")
        mods = "+".join({29: "Ctrl", 56: "Alt", 42: "Shift"}[m] for m in k.mods)
        sc = scancode_hex(op) or "-"
        kc = k.kc if k.kc is not None else "-"
        print(f"{op:<{width}}  {lbl:<12} {str(kc):>4} {mods:<14} {sc:<18} {k.note}")
    unmapped = [op for op, k in KEYS.items() if k.kc is None and op != "leer"]
    print(f"\nunmapped (need live confirmation): {unmapped}")
    sys.exit(0)
