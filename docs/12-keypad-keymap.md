# 12 — The Keypad: Full Button Map & Input Protocol

This documents the complete on-screen keypad (`keypad.exe`) — every button and the exact code
it sends to the control — so a **native, cross-platform keypad** can reproduce it faithfully.
All of this is derived from the keypad's embedded QML, the guest's keymap resources, the
`heuinput` interface, and live validation against the running control.

> The underlying HEIDENHAIN keymap files and `keypad.exe` are proprietary and are **not**
> committed (they live under the git-ignored `work/`). The tables below are factual
> key→code mappings recorded for interoperability — see [09-legal.md](09-legal.md).

## How the keypad works

`keypad.exe` is a Qt6/QML app. The QML defines the visual layouts; a C++ `KeyPad` class
(`keypad.buttonClicked(operation)`) turns each button's **`operation`** string into a keypress
delivered to the control. There are layout variants for different keyboards/controls:

- `keypadNc.qml`, `keypadNcVertical.qml`, `keypadNcVertical-CP640.qml` — the NC keyboard
- `keypadMachine.qml`, `keypadMachineAndNc.qml` — machine operating panel (configurable via
  `machinekeys.ini`) and combined
- `keypadTE757.qml` / a CNC-Pilot panel — uses **LSV2 PLC codes** (`PLC_DMG_F01..F20`)

Buttons reference an icon `qrc:///images/<pngfile>.png` and carry an `operation`; machine
buttons use `operation = "M_" + pngfile`.

### Two transports (by environment)

`keypad.exe` detects the target (`typeOfVm`: 1=VBox, 2=VMware, 3=WindowsTnc) and delivers keys
two ways:

- **Virtual machine (VBox/VMware)** — two equivalent mechanisms are present in the binary:
  - **VirtualBox `putScancodes`** — it injects raw **AT scancode set 1** straight into the
    emulated keyboard via the VirtualBox COM API `IKeyboard::putScancodes` (the binary contains
    the diagnostics `VBox putScancodes: GetKeyboard failed!` / `LockMachine failed!` …). This is
    **identical to `VBoxManage controlvm <vm> keyboardputscancode <bytes…>`** and needs nothing
    inside the guest. *This is the mechanism our native keypad uses by default.*
  - **`heuinput` FIFO** — alternatively it starts the guest service (`/etc/init.d/heuinput
    start`) and appends tokens to the FIFO **`/tmp/__heuinput`** (`… >>/tmp/__heuinput`).
    `heuinput` synthesizes the keypress via Linux **`uinput`**. Token format (from `heuinput`
    itself):
    ```
    KP <code>\n     key press        KR <code>\n     key release
    ```
    where `<code>` is a **Linux input keycode** (decimal, or `0x..` hex). Modifiers are sent as
    their own press/release around the key.
- **Real control on Windows (`WindowsTnc`)** → it connects over TCP and uses the **LSV2
  `KeyPress`** command (HEIDENHAIN's protocol, the one TNCremo also speaks).

Because the VBox path is just `putScancodes`, a host‑side keypad can be **fully faithful** to
the original with no guest cooperation at all — it only needs to convert each `operation` into
the right AT set‑1 make/break bytes (with `CTRL`=`0x1D`, `ALT`=`0x38`, `SHIFT`=`0x2A` wrapped
around the key, extended keys prefixed with `0xE0`).

## Keycode conventions (important)

The guest's MMI interprets **X keycodes** via the keymap `keymap_te530_*_vbox.xml`. The three
keycode spaces relate simply:

```
X keycode  =  Linux/evdev keycode + 8
```

So for the **`heuinput` path** (Linux keycodes): `heuinput_code = (X keycode) − 8`.
For **`VBoxManage controlvm <vm> keyboardputscancode`** (raw PS/2 Set-1) send the Set-1 code
that maps to that Linux keycode. Modifiers (sent as separate keys): `CTRL`=Linux 29, `ALT`=56,
`SHIFT`=42.

**Validated live:** soft-key-1 `HORZSOFTKEY1` (X kc `0x43`=67) dismisses dialogs; `KEY_TNC_CE`
(X kc `0x5B`=91) clears "Power interrupted". Both confirmed on the running TNC 640.

## The keymap — every special key

`scancode` below is the **X keycode** the MMI expects (`keymap_te530_1024_vbox.xml`,
hardware = "Heidenhain TE530 [X-Windows on VirtualBox]"). `heuinput` code = `scancode − 8`.
Standard keys (digits, ENT, ESC, cursor, Backspace, …) are **not** remapped — the keypad sends
them as ordinary PC keycodes.

### Soft keys
| virtualKey | scancode | modifier |
|---|---|---|
| HORZSOFTKEY1 … HORZSOFTKEY8 | 0x43 … 0x4A | — |
| SOFTKEYSCROLLLEFT / RIGHT | 0x4B / 0x4C | — |
| SOFTKEYSCROLLUP | 0x13 | SHIFT+CTRL+ALT |
| VERTSOFTKEY1 … VERTSOFTKEY6 | 0x0A … 0x0F | SHIFT+CTRL+ALT |

### Axes, position, entry (no modifier)
| virtualKey | scancode |  | virtualKey | scancode |
|---|---|---|---|---|
| TNC_AXISX | 0x4F | | TNC_GOTO | 0x54 |
| TNC_AXISY | 0x53 | | TNC_Q_PARAM | 0x55 |
| TNC_AXISZ | 0x57 | | TNC_NOENT | 0x56 |
| TNC_AXIS4 | 0x50 | | TNC_INCREMENT | 0x59 |
| TNC_AXIS5 | 0x58 | | TNC_ACTPOS | 0x6A |
| TNC_POLAR | 0x51 | | TNC_DECIMAL | 0x3F |
| TNC_PLUSMINUS | 0x52 | | TNC_CE | 0x5B |
| TNC_END_BLOCK | 0x73 | | TNC_DEL_BLOCK | 0x77 |
| SCREEN_MANAGEMENT | 0x5F | | SCREEN_CHANGE | 0x60 |

### Popups / function (modifier = CTRL+ALT)
| virtualKey | scancode |  | virtualKey | scancode |
|---|---|---|---|---|
| TNC_PGM_MGT | 0x21 | | TNC_ERR | 0x2E |
| TNC_CALC | 0x39 | | TNC_HELP | 0x2B |
| TNC_MOD | 0x3A | | F12 (screen change) | 0x2D |

### Operating modes (modifier = CTRL+ALT)
| virtualKey | scancode |  | virtualKey | scancode |
|---|---|---|---|---|
| TNC_MANUAL | 0x0A | | TNC_MDI | 0x18 |
| TNC_HANDWHEEL | 0x0B | | TNC_SINGLE | 0x19 |
| TNC_SMARTNC | 0x0C | | TNC_FULL | 0x1A |
| TNC_EDIT | 0x0D | | TNC_TEST | 0x1B |

### Contour / path programming (modifier = CTRL+ALT)
| virtualKey | scancode |  | virtualKey | scancode |
|---|---|---|---|---|
| TNC_APPR_DEP | 0x0E | | TNC_CIRCLERADIUS (CR) | 0x1C |
| TNC_FK | 0x0F | | TNC_RND | 0x1D |
| TNC_CHF | 0x11 | | TNC_CIRCLETANGENTIAL (CT) | 0x1E |
| TNC_LINE | 0x12 | | TNC_CENTERCIRCLE (CC) | 0x1F |
|  |  | | TNC_CIRCLE (C) | 0x20 |

### Cycles, tools, labels, program (modifier = CTRL+ALT)
| virtualKey | scancode |  | virtualKey | scancode |
|---|---|---|---|---|
| TNC_TOUCH_PROBE | 0x26 | | TNC_STOP | 0x34 |
| TNC_CYCL_DEF | 0x27 | | TNC_TOOL_DEF | 0x35 |
| TNC_CYCL_CALL | 0x28 | | TNC_TOOL_CALL | 0x36 |
| TNC_LBL_SET | 0x29 | | TNC_SPEC_FCT | 0x37 |
| TNC_LBL_CALL | 0x2A | | TNC_PGM_CALL | 0x38 |

### smartNC navigation & misc
| virtualKey | scancode | modifier |
|---|---|---|
| TNC_SMARTNC_PGDOWN | 0x75 | CTRL |
| TNC_SMARTNC_UP | 0x6F | CTRL+ALT |
| TNC_SMARTNC_DOWN | 0x74 | CTRL+ALT |
| TNC_CONNECT | 0x26 | CTRL+ALT+SHIFT |

> Note: some scancodes are reused with different modifiers (e.g. `0x0A` = MANUAL with CTRL+ALT
> but VERTSOFTKEY1 with SHIFT+CTRL+ALT). The modifier set disambiguates — always send the
> exact modifier combination.

## Full button inventory (operation strings, from the QML)

NC keyboard (`operation` values): `0–9`, `NUM_0–NUM_9`, `X Y Z IV V`, `Q`, `+/-`, `,`,
`ENT`, `NO ENT`, `CE`, `BS`, `GOTO`, `HOME`, `END`, `PGUP`, `PGDN`, `CURUP/DOWN/LEFT/RIGHT`,
`F1 F2 F9 F10 F11 F12`, soft-key scroll (`SKLEFT/RIGHT/UP/DOWN`, `VSKDOWN`),
`OM_MACHINE OM_PROGRAM OM_ORGANIZE OM_TOOLDATA` (modes), `AUTO MANUAL SINGLE SIMU SMART INC
HANDW STOP`, `MOD ERR HELP CALC INFO`, `PGMMGT SPEC-FCT`, `CYCLDEF CYCLCALL TOOLDEF TOOLCALL
LBLSET LBLCALL PGMCALL`, `APRDEP FK CHF CT CC CR RND LINE POLAR`, `S-OV V-OV` (overrides),
`ACTPOS PRTSC EDIT INS DOMORE TI TPR`, screen change (`bildwechsel2/3-fach`),
`steuern_handeingabe`, plus `_CP` variants for CNC-Pilot.

Machine panel (`operation` values): `power_on`, `nc_start`, `nc_stop`, `emergency_stop`,
`PLC_DMG_F01 … PLC_DMG_F20` (PLC soft keys via LSV2), and `machinekeys/*` icons
(`chuck_mainspindle`, `tailstock`, `machine_status`, …) — fully configurable via
`machinekeys.ini`.

(Each NC `operation` corresponds to a `virtualKey` above by name, e.g. `CE`→`KEY_TNC_CE`,
`PGMMGT`→`KEY_TNC_PGM_MGT`, `F1`→`HORZSOFTKEY1`, `X`→`KEY_TNC_AXISX`.)

## Driving the control (worked examples)

For a native keypad targeting a VirtualBox guest, write to `/tmp/__heuinput` (Linux keycode =
scancode − 8), wrapping modifiers. Examples:

```sh
# CE  (KEY_TNC_CE, scancode 0x5B=91 -> heuinput 83):
printf 'KP 83\nKR 83\n' >/tmp/__heuinput

# Soft key 1 (HORZSOFTKEY1, 0x43=67 -> 59):
printf 'KP 59\nKR 59\n' >/tmp/__heuinput

# PGM MGT (scancode 0x21=33 -> 25, modifier CTRL+ALT = 29 + 56):
printf 'KP 29\nKP 56\nKP 25\nKR 25\nKR 56\nKR 29\n' >/tmp/__heuinput

# Operating mode "Test" (TNC_TEST, 0x1B=27 -> 19, CTRL+ALT):
printf 'KP 29\nKP 56\nKP 19\nKR 19\nKR 56\nKR 29\n' >/tmp/__heuinput
```

(`heuinput` runs as root and reads the FIFO; on a real install `keypad.exe` starts it. Standard
keys — digits, ENT, ESC, cursor — are sent as their ordinary keycodes with no modifier.)

Equivalent low-level injection used during analysis (no in-guest service needed):
`VBoxManage controlvm <vm> keyboardputscancode <PS/2 Set-1 make> <break>` — e.g. CE = `53 d3`,
soft-key-1 = `3b bb`.

## Building a native keypad

1. **Layout/labels/icons:** reuse the recovered QML structure + the `tnckeys/` and
   `machinekeys/` icon names as a reference for a from-scratch UI (don't ship HEIDENHAIN's
   assets).
2. **Per button:** map `operation` → `virtualKey` → (scancode, modifier) from the tables above.
3. **Send** via the chosen transport (heuinput FIFO for the VM; or the low-level injection).
4. **Resolutions:** keymap variants exist for 1024/1280/1920 (`keymap_te530_*_vbox.xml`); the
   scancodes are the same — resolution only affects the on-screen layout sizing.

This makes a faithful, cross-platform keypad possible with **no proprietary redistribution** and
no dependency on `keypad.exe`.

## Implemented: `keypad/` (native, cross‑platform)

A working clean‑room keypad lives in [`../keypad/`](../keypad/) (PySide6 / Qt 6, Python):

- `tnckeymap.py` — the full `operation → Linux keycode (+modifiers) → AT set‑1 scancodes /
  heuinput tokens` table, with a self‑test. Run it to dump the table.
- `transport.py` — `vbox` (default, `putScancodes`), `heuinput`, and `dry` backends.
- `tnc_keypad.py` — the GUI, reproducing **both** original NC panels:
  `--layout vertical` (`qml_07`, what `keypad.exe -nv` / the desktop launcher opens — the default)
  and `--layout horizontal` (`qml_08`, `keypad.exe -n`). Both layouts use the **same** `operation`
  set and therefore the same key codes; only the arrangement differs.

**Live validation (vbox transport, against the running TNC 640 in demo mode):**

| Key | Sent (AT set‑1) | Result |
|---|---|---|
| `CE` | `53 d3` | clears entry / dialogs |
| soft‑key 1 (F1) | `3b bb` | presses horizontal soft key 1 |
| `MOD` (Ctrl+Alt+M) | `1d 38 32 b2 b8 9d` | opens Settings/MOD window |
| `EDIT` (Ctrl+Alt+4) | `1d 38 05 85 b8 9d` | switches to Programming mode |
| `PGMMGT` (Ctrl+Alt+P) | `1d 38 19 99 b8 9d` | opens file management (Programming mode) |

Both no‑modifier keys and Ctrl+Alt combinations work via a single `putScancodes` burst.
Operations that the control gates by context (e.g. `PGMMGT` only opens file management while in
Programming mode) behave exactly as on the real machine. One key, `VSKDOWN` (vertical soft‑key
scroll‑down), has no entry in the guest keymap and is left unmapped pending further live testing.
