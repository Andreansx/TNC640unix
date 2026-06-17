# 14 — Install & Run (the manual)

How to run the **HEIDENHAIN TNC 640 programming station** on a Linux PC using this
repository plus the official HEIDENHAIN package. One command sets it up; one command
runs it (the control VM + the on‑screen keypad).

> **You must obtain the HEIDENHAIN software yourself.** This repository contains
> **only** glue code and documentation — no HEIDENHAIN or Oracle binaries. Running the
> programming station in **demo mode** needs no dongle and no license (it's limited to
> 100 NC lines / 10 CAD elements). See [09‑legal.md](09-legal.md).

---

## 1. What you need

| | |
|---|---|
| **A Linux PC, x86‑64** | The control VM is x86‑64. ARM hosts (e.g. Apple Silicon) can only emulate it, slowly — see [08‑porting‑unix‑macos.md](08-porting-unix-macos.md). |
| **VirtualBox 7.1+** | The designed hypervisor. Install from your distro or <https://www.virtualbox.org/wiki/Linux_Downloads>. The kernel module `vboxdrv` must build/load. |
| **Python 3.8+** | For the keypad (PySide6 is installed into a local venv for you). |
| **~10 GB free disk** | For the imported VM and the unpacked NC software. |
| **The HEIDENHAIN package** | The TNC 640 programming station download (NC ident **340595**, version **18 SP4**). See §2. |

Hardware acceleration (`/dev/kvm`) is used automatically if present; without it the VM
still runs, just slower.

## 2. Get the HEIDENHAIN package

Download the **TNC 640 programming station** ("PGM‑Platz Virtual") from HEIDENHAIN:

> **Official download:** <https://www.heidenhain.com/service/downloads/software>
>
> On that page, find the **TNC 640** programming station / "Programming Station" software
> (NC ident **340595**, version **18 SP4** is what this project was built against; newer SPs
> should work the same way). You may need a free HEIDENHAIN account. The download is also
> reachable via HEIDENHAIN's *TNCmanager*.

Unpack it somewhere. The folder you want is the one that contains, somewhere beneath it:

```
.../Setup/base/TNCvbProg.ova        <- the VM image
.../Setup/prog/setup.zip            <- the NC software flashed on first boot
```

Remember that folder's path — you'll pass it to `setup` as `--package`. (You can also
drop the unpacked download into the repo as `34059518/…` and it will be auto‑detected;
it's git‑ignored.)

## 3. Install

```sh
git clone https://github.com/Andreansx/TNC640unix
cd TNC640unix

# 3a. Check your machine is ready (VirtualBox, kernel module, Python, the package):
./tnc640 doctor

# 3b. Install the keypad's Python dependency (PySide6) into a local venv:
./tnc640 keypad --install-deps

# 3c. First-time provisioning: import the VM, wire it up, flash the NC software.
#     Point --package at your unpacked download (omit it if you used 34059518/).
./tnc640 setup --package /path/to/your/HEIDENHAIN/download
```

`setup` imports the OVA, configures the VM (CPUs/RAM, NAT, the `Install`/`IOsim`/`PLC`/`TNC`
shared folders, the display property), stages `setup.zip`, and starts the VM. **The first
boot installs the NC software and takes ~15–20 minutes** (copy → extract ~657 MB → install
RPMs → reboot). Just watch the VM window until the TNC 640 control appears (in demo mode).
On a headless box add `--headless` and poll with `./tnc640 shot`.

## 4. Run (everyday use)

```sh
./tnc640 run
```

This starts the control VM (if it isn't already running) and opens the **keypad** — the
vertical NC panel, the same one HEIDENHAIN's desktop launcher opens. Use the control in the
VM window; click keypad buttons to drive it.

```sh
./tnc640 run --layout horizontal   # the horizontal keypad instead
./tnc640 run --fullscreen          # control VM fullscreen
./tnc640 run --no-keypad           # VM only
./tnc640 keypad                    # add/restart the keypad against a running VM
./tnc640 stop                      # power the control off
./tnc640 status                    # is it running?
./tnc640 shot [file.png]           # screenshot the control
```

## 5. The keypad

The keypad reproduces the original NC panel button‑for‑button and sends the same key codes
via VirtualBox's `putScancodes` (exactly what HEIDENHAIN's `keypad.exe` does for a VBox VM).
Full details and the complete key map are in [12‑keypad‑keymap.md](12-keypad-keymap.md);
the keypad's own notes are in [`../keypad/README.md`](../keypad/README.md).

**Soft keys (F1–F8):** the NC keypad has no soft keys — those live on the control's screen
unit. Click them directly in the VM window, or press **F1–F8** on your PC keyboard (they reach
the control unchanged).

## 6. Configuration

Defaults work out of the box. To override (VM name, RAM, package path, SSH port), copy the
example and edit it:

```sh
cp config.env.example config.env
```

`config.env` is git‑ignored.

## 7. Troubleshooting

| Symptom | Fix |
|---|---|
| `VBoxManage not found` | Install VirtualBox 7.1+ and ensure it's on `PATH`. |
| VM won't start / `vboxdrv` errors | `sudo modprobe vboxdrv` (or reinstall the VirtualBox kernel module: `sudo /sbin/vboxconfig`). |
| First boot never finishes | Give it 15–20 min; verify `setup.zip` is in `work/vm/Install/`. Check progress with `./tnc640 shot`. |
| Keypad won't open | `./tnc640 keypad --install-deps`, then `./tnc640 keypad`. |
| "Shareware: max 100 NC lines" | Expected — that's demo mode (no dongle). Programming and test‑run/simulation work within the limit. |
| Slow / laggy | Ensure `/dev/kvm` exists (`./tnc640 doctor`) and your user can use it. |
| Machine power‑on / operating modes need PLC I/O | The PLC‑I/O simulator (JHIO) is Windows‑only today; programming + test run don't need it. See [06‑bridge‑and‑io.md](06-bridge-and-io.md). |

## 8. What this does and doesn't include

- **Includes:** the `tnc640` launcher, the provisioning recipe, and the native keypad — all
  original, clean‑room work.
- **Does not include:** any HEIDENHAIN or Oracle file. You download VirtualBox and the
  HEIDENHAIN package yourself; nothing proprietary is redistributed here. See
  [09‑legal.md](09-legal.md).
