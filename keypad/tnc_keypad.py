#!/usr/bin/env python3
"""
tnc_keypad.py - a native, cross-platform on-screen keypad for the HEIDENHAIN
TNC 640 programming station running under VirtualBox on UNIX-like systems.

It reproduces the button layout of the original keypad.exe NC panel - both the
vertical panel (keypad.exe's "-nv", the one the desktop launcher opens) and the
horizontal panel ("-n") - and sends, for every button, the same keypress the
original would: by default through VirtualBox's putScancodes, exactly what
keypad.exe does for a VBox VM (IKeyboard::putScancodes). No HEIDENHAIN code or
icons are used; labels are plain text and the key codes are documented in
docs/12-keypad-keymap.md.

Run:
    python3 tnc_keypad.py --vm TNC640                 # vertical panel -> running VM
    python3 tnc_keypad.py --layout horizontal --vm TNC640
    python3 tnc_keypad.py --transport dry             # just log, no VM
    python3 tnc_keypad.py --layout vertical --screenshot v.png   # render headless

Requires: PySide6  (pip install PySide6-Essentials)
"""
import argparse
import sys

import tnckeymap
import transport as tp

# ---------------------------------------------------------------------------
# HORIZONTAL layout (keypad QML qml_08, "NC Steuerungsbedienfeld horizontal").
# A row of panels; each inner list is one row, "leer" is a blank filler.
# ---------------------------------------------------------------------------
H_GRID1 = [
    ["F11", "F9", "VSKDOWN", "F10", "F12"],
    ["PGMMGT", "CALC", "MOD", "HELP", "ERR"],
    ["leer", "leer", "leer", "leer", "leer"],
    ["MANUAL", "HANDW", "SMART", "leer", "EDIT"],
    ["TI", "SINGLE", "AUTO", "leer", "SIMU"],
]
H_GRID2 = [
    ["APRDEP", "FK", "M", "CHF", "LINE"],
    ["CR", "RND", "CT", "CC", "C"],
    ["leer", "leer", "leer", "leer", "leer"],
    ["TPR", "CYCLDEF", "CYCLCALL", "LBLSET", "LBLCALL"],
    ["STOP", "TOOLDEF", "TOOLCALL", "SPEC-FCT", "PGMCALL"],
]
H_GRID3 = [
    ["X", "7", "8", "9"],
    ["Y", "4", "5", "6"],
    ["Z", "1", "2", "3"],
    ["IV", "0", ",", "+/-"],
    ["V", "BS", "ACTPOS", "Q"],
]
H_GRID4 = [
    ["NB_PGDN", "HOME", "CURUP", "PGUP"],
    ["FRMUP", "CURLEFT", "GOTO", "CURRIGHT"],
    ["FRMDOWN", "CEND", "CURDOWN", "PGDN"],
    ["CE", "ZDEL", "POLAR", "INC"],
]

# ---------------------------------------------------------------------------
# VERTICAL layout (keypad QML qml_07, "NC Steuerungsbedienfeld vertikal").
# A single 5-column-wide stack of blocks; blocks are separated by a gap.
# Each block is a grid of rows. A block "ENT" is the NO ENT / ENT(2-wide) / END
# row. "leer" keeps a cell blank to preserve geometry.
# ---------------------------------------------------------------------------
V_BLOCKS = [
    [["F11", "F9", "VSKDOWN", "F10", "F12"]],
    [["PGMMGT", "CALC", "MOD", "HELP", "ERR"]],
    [["MANUAL", "HANDW", "SMART", "leer", "EDIT"],
     ["TI", "SINGLE", "AUTO", "leer", "SIMU"]],
    [["APRDEP", "FK", "M", "CHF", "LINE"],
     ["CR", "RND", "CT", "CC", "C"]],
    [["TPR", "CYCLDEF", "CYCLCALL", "LBLSET", "LBLCALL"],
     ["STOP", "TOOLDEF", "TOOLCALL", "SPEC-FCT", "PGMCALL"]],
    [["leer", "X", "7", "8", "9"],
     ["leer", "Y", "4", "5", "6"],
     ["leer", "Z", "1", "2", "3"],
     ["leer", "IV", "0", ",", "+/-"],
     ["leer", "V", "BS", "ACTPOS", "Q"],
     ["leer", "CE", "ZDEL", "POLAR", "INC"]],
    "ENT",   # special row: leer, NO ENT, ENT(2-wide), END
    [["leer", "NB_PGDN", "HOME", "CURUP", "PGUP"],
     ["leer", "FRMUP", "CURLEFT", "GOTO", "CURRIGHT"],
     ["leer", "FRMDOWN", "CEND", "CURDOWN", "PGDN"]],
]

# colour families (subtle, just to group the panel like the real one)
FAMILY = {
    "mode": ({"MANUAL", "HANDW", "SMART", "EDIT", "TI", "SINGLE", "AUTO", "SIMU"}, "#5C7A99"),
    "popup": ({"PGMMGT", "CALC", "MOD", "HELP", "ERR", "F9", "F10", "F11", "F12", "VSKDOWN"}, "#6E6E6E"),
    "axis": ({"X", "Y", "Z", "IV", "V", "Q", "POLAR", "INC"}, "#7A6E55"),
    "entry": ({"ENT", "NO\nENT", "ZEND", "CE", "ZDEL", "ACTPOS", "GOTO", "BS"}, "#4A6B4A"),
}


def family_color(op):
    for members, color in FAMILY.values():
        if op in members:
            return color
    return "#3C4650"  # default dark slate (number/function keys)


def _build_qt():
    from PySide6 import QtCore, QtWidgets

    class KeyButton(QtWidgets.QPushButton):
        def __init__(self, op, on_press):
            k = tnckeymap.KEYS.get(op)
            super().__init__(k.label if k else op)
            self.op = op
            self.setMinimumSize(QtCore.QSize(52, 42))
            self.setSizePolicy(QtWidgets.QSizePolicy.Expanding,
                               QtWidgets.QSizePolicy.Expanding)
            mapped = bool(k and k.mapped)
            sc = tnckeymap.scancode_hex(op)
            tip = [f"operation: {op}"]
            if mapped:
                tip.append(f"scancode: {sc}")
                tip.append(f"heuinput: {tnckeymap.heuinput_tokens(op).strip().replace(chr(10), ' ')}")
            else:
                tip.append("(unmapped - confirm live)")
            if k and k.note:
                tip.append(k.note)
            self.setToolTip("\n".join(tip))
            color = family_color(op)
            border = "#2A2F36" if mapped else "#B03030"
            css = (f"QPushButton{{background:{color};color:#EDEDED;border:1px solid {border};"
                   f"border-radius:5px;font:600 11px 'DejaVu Sans';padding:1px;}}"
                   f"QPushButton:hover{{background:#7C8A99;}}"
                   f"QPushButton:pressed{{background:#26415E;}}")
            if not mapped:
                css += "QPushButton{color:#FFD0D0;font-style:italic;}"
            self.setStyleSheet(css)
            self.clicked.connect(lambda: on_press(op))

    def spacer():
        w = QtWidgets.QWidget()
        w.setMinimumSize(52, 42)
        w.setSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding)
        return w

    def make_grid(rows, on_press):
        """A QGridLayout widget from a 2D op list. 'leer' -> blank cell."""
        w = QtWidgets.QWidget()
        g = QtWidgets.QGridLayout(w)
        g.setSpacing(4)
        g.setContentsMargins(0, 0, 0, 0)
        for r, row in enumerate(rows):
            for c, op in enumerate(row):
                g.addWidget(spacer() if op == "leer" else KeyButton(op, on_press), r, c)
        return w

    def make_entry_row(on_press, leading_blank):
        """The NO ENT / ENT(2-wide) / END row (optionally with a leading blank)."""
        w = QtWidgets.QWidget()
        g = QtWidgets.QGridLayout(w)
        g.setSpacing(4)
        g.setContentsMargins(0, 0, 0, 0)
        col = 0
        if leading_blank:
            g.addWidget(spacer(), 0, col); col += 1
        g.addWidget(KeyButton("NO\nENT", on_press), 0, col); col += 1
        g.addWidget(KeyButton("ENT", on_press), 0, col, 1, 2); col += 2  # double width
        g.addWidget(KeyButton("ZEND", on_press), 0, col)
        return w

    def build_horizontal(on_press):
        root = QtWidgets.QWidget()
        h = QtWidgets.QHBoxLayout(root)
        h.setSpacing(12)
        h.setContentsMargins(0, 0, 0, 0)
        h.addWidget(make_grid(H_GRID1, on_press))
        h.addWidget(make_grid(H_GRID2, on_press))
        h.addWidget(make_grid(H_GRID3, on_press))
        col = QtWidgets.QWidget()
        v = QtWidgets.QVBoxLayout(col)
        v.setSpacing(4)
        v.setContentsMargins(0, 0, 0, 0)
        v.addWidget(make_grid(H_GRID4, on_press))
        v.addWidget(make_entry_row(on_press, leading_blank=False))
        h.addWidget(col)
        return root

    def build_vertical(on_press):
        root = QtWidgets.QWidget()
        v = QtWidgets.QVBoxLayout(root)
        v.setSpacing(13)            # larger gap between blocks, like the QML
        v.setContentsMargins(0, 0, 0, 0)
        for block in V_BLOCKS:
            if block == "ENT":
                v.addWidget(make_entry_row(on_press, leading_blank=True))
            else:
                v.addWidget(make_grid(block, on_press))
        return root

    class KeyPad(QtWidgets.QWidget):
        def __init__(self, transport, layout):
            super().__init__()
            self.transport = transport
            self.setWindowTitle(f"TNC 640 keypad ({layout}) - TNC640unix")
            self.setStyleSheet("QWidget{background:#B2BAC0;}")
            root = QtWidgets.QVBoxLayout(self)
            root.setContentsMargins(8, 8, 8, 8)
            root.setSpacing(6)
            panel = build_vertical(self.press) if layout == "vertical" else build_horizontal(self.press)
            root.addWidget(panel)

            self.status = QtWidgets.QLabel("")
            self.status.setWordWrap(True)
            self._ok_css = ("QLabel{background:#1E232A;color:#9FE0A0;"
                            "font:10px 'DejaVu Sans Mono';padding:4px;border-radius:4px;}")
            self._err_css = self._ok_css.replace("#9FE0A0", "#E0A0A0")
            self.status.setStyleSheet(self._ok_css)
            ok, msg = transport.available()
            self.status.setText(f"transport: {transport.name}  |  {msg}")
            if not ok:
                self.status.setStyleSheet(self._err_css)
            root.addWidget(self.status)

        def press(self, op):
            try:
                msg = self.transport.send(op)
                self.status.setStyleSheet(self._ok_css)
            except tp.TransportError as e:
                msg = f"ERROR sending {op!r}: {e}"
                self.status.setStyleSheet(self._err_css)
            self.status.setText(msg)
            print(msg, flush=True)

    return QtWidgets, KeyPad


def main(argv=None):
    ap = argparse.ArgumentParser(description="Native TNC 640 keypad")
    ap.add_argument("--layout", choices=["vertical", "horizontal"], default="vertical",
                    help="panel layout (default vertical, as the desktop launcher opens)")
    ap.add_argument("--transport", choices=["vbox", "heuinput", "dry"], default="vbox")
    ap.add_argument("--vm", default="TNC640", help="VirtualBox VM name (vbox transport)")
    ap.add_argument("--vboxmanage", default="VBoxManage")
    ap.add_argument("--heu-cmd", default=None,
                    help="shell command that pipes stdin into guest /tmp/__heuinput")
    ap.add_argument("--screenshot", default=None,
                    help="render the keypad to this PNG and exit (headless OK)")
    args = ap.parse_args(argv)

    transport = tp.make_transport(args.transport, vm=args.vm,
                                  vboxmanage=args.vboxmanage, heu_cmd=args.heu_cmd)

    QtWidgets, KeyPad = _build_qt()
    app = QtWidgets.QApplication(sys.argv[:1])
    win = KeyPad(transport, args.layout)

    size = (300, 940) if args.layout == "vertical" else (1180, 320)
    win.resize(*size)
    win.show()

    if args.screenshot:
        app.processEvents()
        pix = win.grab()
        pix.save(args.screenshot, "PNG")
        print(f"wrote {args.screenshot} ({pix.width()}x{pix.height()})")
        return 0
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
