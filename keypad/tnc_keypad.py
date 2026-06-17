#!/usr/bin/env python3
"""
tnc_keypad.py - a native, cross-platform on-screen keypad for the HEIDENHAIN
TNC 640 programming station running under VirtualBox on UNIX-like systems.

It reproduces the button layout of the original keypad.exe NC panel
("NC Steuerungsbedienfeld horizontal") and sends, for every button, the same
keypress the original would - by default through VirtualBox's putScancodes
(exactly what keypad.exe does for a VBox VM). No HEIDENHAIN code or icons are
used; labels are plain text and the key codes are documented in
docs/12-keypad-keymap.md.

Run:
    python3 tnc_keypad.py --vm TNC640                 # send to running VM
    python3 tnc_keypad.py --transport dry             # just log, no VM
    python3 tnc_keypad.py --screenshot out.png        # render layout headless

Requires: PySide6  (pip install PySide6-Essentials)
"""
import argparse
import sys

import tnckeymap
import transport as tp

# ---------------------------------------------------------------------------
# Layout, taken verbatim from the keypad QML (qml_08). Each inner list is one
# row, left to right; "leer" is a blank filler that keeps the grid geometry.
# ---------------------------------------------------------------------------
GRID1 = [  # screen/softkey nav, popups, operating modes
    ["F11", "F9", "VSKDOWN", "F10", "F12"],
    ["PGMMGT", "CALC", "MOD", "HELP", "ERR"],
    ["leer", "leer", "leer", "leer", "leer"],
    ["MANUAL", "HANDW", "SMART", "leer", "EDIT"],
    ["TI", "SINGLE", "AUTO", "leer", "SIMU"],
]
GRID2 = [  # contour/path, cycles, tools, program
    ["APRDEP", "FK", "M", "CHF", "LINE"],
    ["CR", "RND", "CT", "CC", "C"],
    ["leer", "leer", "leer", "leer", "leer"],
    ["TPR", "CYCLDEF", "CYCLCALL", "LBLSET", "LBLCALL"],
    ["STOP", "TOOLDEF", "TOOLCALL", "SPEC-FCT", "PGMCALL"],
]
GRID3 = [  # axes + numeric entry
    ["X", "7", "8", "9"],
    ["Y", "4", "5", "6"],
    ["Z", "1", "2", "3"],
    ["IV", "0", ",", "+/-"],
    ["V", "BS", "ACTPOS", "Q"],
]
GRID4 = [  # navigation / editing cluster
    ["NB_PGDN", "HOME", "CURUP", "PGUP"],
    ["FRMUP", "CURLEFT", "GOTO", "CURRIGHT"],
    ["FRMDOWN", "CEND", "CURDOWN", "PGDN"],
    ["CE", "ZDEL", "POLAR", "INC"],
]
# entry row under GRID4: NO ENT (1), ENT (2-wide), END (1)
ENTRY = ["NO\nENT", "ENT", "ZEND"]

# colour families (subtle, just to group the panel like the real one)
FAMILY = {
    "mode": ({"MANUAL", "HANDW", "SMART", "EDIT", "TI", "SINGLE", "AUTO", "SIMU"}, "#5C7A99"),
    "popup": ({"PGMMGT", "CALC", "MOD", "HELP", "ERR", "F9", "F10", "F11", "F12", "VSKDOWN"}, "#6E6E6E"),
    "axis": ({"X", "Y", "Z", "IV", "V", "Q", "POLAR", "INC"}, "#7A6E55"),
    "entry": ({"ENT", "NO\nENT", "ZEND", "CE", "ZDEL", "ACTPOS", "GOTO", "BS"}, "#4A6B4A"),
}


def family_color(op):
    for _, (members, color) in FAMILY.items():
        if op in members:
            return color
    return "#3C4650"  # default dark slate (number/function keys)


def _build_qt(args):
    from PySide6 import QtCore, QtGui, QtWidgets

    class KeyButton(QtWidgets.QPushButton):
        def __init__(self, op, on_press):
            k = tnckeymap.KEYS.get(op)
            label = k.label if k else op
            super().__init__(label)
            self.op = op
            self.setMinimumSize(QtCore.QSize(58, 44))
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
            self.setStyleSheet(
                f"QPushButton{{background:{color};color:#EDEDED;border:1px solid {border};"
                f"border-radius:5px;font:600 11px 'DejaVu Sans';padding:1px;}}"
                f"QPushButton:hover{{background:#7C8A99;}}"
                f"QPushButton:pressed{{background:#26415E;}}")
            if not mapped:
                self.setStyleSheet(self.styleSheet() +
                                   "QPushButton{color:#FFD0D0;font-style:italic;}")
            self.clicked.connect(lambda: on_press(op))

    def make_grid(rows, on_press, span_last_entry=False):
        w = QtWidgets.QWidget()
        g = QtWidgets.QGridLayout(w)
        g.setSpacing(4)
        g.setContentsMargins(0, 0, 0, 0)
        for r, row in enumerate(rows):
            for c, op in enumerate(row):
                if op == "leer":
                    spacer = QtWidgets.QWidget()
                    spacer.setMinimumSize(58, 44)
                    spacer.setSizePolicy(QtWidgets.QSizePolicy.Expanding,
                                         QtWidgets.QSizePolicy.Expanding)
                    g.addWidget(spacer, r, c)
                else:
                    g.addWidget(KeyButton(op, on_press), r, c)
        return w

    class KeyPad(QtWidgets.QWidget):
        def __init__(self, transport):
            super().__init__()
            self.transport = transport
            self.setWindowTitle("TNC 640 - virtual NC keyboard (TNC640unix)")
            self.setStyleSheet("QWidget{background:#B2BAC0;}")
            root = QtWidgets.QVBoxLayout(self)
            root.setContentsMargins(8, 8, 8, 8)

            panels = QtWidgets.QHBoxLayout()
            panels.setSpacing(12)
            panels.addWidget(make_grid(GRID1, self.press), 0)
            panels.addWidget(make_grid(GRID2, self.press), 0)
            panels.addWidget(make_grid(GRID3, self.press), 0)

            # column on the right: grid4 + entry row
            col = QtWidgets.QVBoxLayout()
            col.setSpacing(4)
            col.addWidget(make_grid(GRID4, self.press), 1)
            entry = QtWidgets.QGridLayout()
            entry.setSpacing(4)
            entry.addWidget(KeyButton("NO\nENT", self.press), 0, 0)
            ent_btn = KeyButton("ENT", self.press)
            entry.addWidget(ent_btn, 0, 1, 1, 2)  # double width
            entry.addWidget(KeyButton("ZEND", self.press), 0, 3)
            ew = QtWidgets.QWidget()
            ew.setLayout(entry)
            col.addWidget(ew, 0)
            cw = QtWidgets.QWidget()
            cw.setLayout(col)
            panels.addWidget(cw, 0)

            root.addLayout(panels)

            self.status = QtWidgets.QLabel("")
            self.status.setStyleSheet("QLabel{background:#1E232A;color:#9FE0A0;"
                                      "font:11px 'DejaVu Sans Mono';padding:4px;border-radius:4px;}")
            ok, msg = transport.available()
            self.status.setText(f"transport: {transport.name}   |   {msg}")
            if not ok:
                self.status.setStyleSheet(self.status.styleSheet().replace("#9FE0A0", "#E0A0A0"))
            root.addWidget(self.status)

        def press(self, op):
            try:
                msg = self.transport.send(op)
            except tp.TransportError as e:
                msg = f"ERROR sending {op!r}: {e}"
                self.status.setStyleSheet(self.status.styleSheet().replace("#9FE0A0", "#E0A0A0"))
            self.status.setText(msg)
            print(msg, flush=True)

    return QtWidgets, KeyPad


def main(argv=None):
    ap = argparse.ArgumentParser(description="Native TNC 640 keypad")
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

    QtWidgets, KeyPad = _build_qt(args)
    app = QtWidgets.QApplication(sys.argv[:1])
    win = KeyPad(transport)

    if args.screenshot:
        win.resize(1180, 320)
        win.show()
        app.processEvents()
        pix = win.grab()
        pix.save(args.screenshot, "PNG")
        print(f"wrote {args.screenshot} ({pix.width()}x{pix.height()})")
        return 0

    win.resize(1180, 320)
    win.show()
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
