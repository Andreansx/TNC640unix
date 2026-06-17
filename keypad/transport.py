#!/usr/bin/env python3
"""
transport.py - ways to deliver a keypress to the running TNC 640 guest.

Backends (pick with --transport):

  vbox      VBoxManage controlvm <vm> keyboardputscancode <bytes...>
            Host-side, needs nothing inside the guest. This is what the original
            keypad.exe does for a VirtualBox VM (IKeyboard::putScancodes).  DEFAULT.

  heuinput  Write "KP/KR <linux-keycode>" tokens to the guest FIFO /tmp/__heuinput.
            Faithful to the keypad.exe VM path, but needs the guest heuinput daemon
            running and a way to reach the FIFO. The command used to deliver a line
            into the guest is configurable (default: an ssh one-liner); set it with
            --heu-cmd "ssh ... 'cat >> /tmp/__heuinput'".

  dry       Print what would be sent. No VM needed - for development.
"""
import shlex
import subprocess

import tnckeymap


class TransportError(RuntimeError):
    pass


class DryRunTransport:
    name = "dry"

    def send(self, op):
        sc = tnckeymap.scancode_hex(op)
        heu = tnckeymap.heuinput_tokens(op)
        heu1 = heu.replace("\n", " ").strip() if heu else None
        return f"[dry] {op!r:12} scancode={sc!r}  heuinput={heu1!r}"

    def available(self):
        return True, "dry-run always available"


class VBoxScancodeTransport:
    """Send AT set-1 scancodes through VBoxManage (host-side, like keypad.exe)."""
    name = "vbox"

    def __init__(self, vm, vboxmanage="VBoxManage"):
        self.vm = vm
        self.vboxmanage = vboxmanage

    def available(self):
        try:
            out = subprocess.run([self.vboxmanage, "list", "runningvms"],
                                 capture_output=True, text=True, timeout=10)
        except FileNotFoundError:
            return False, f"{self.vboxmanage} not found on PATH"
        except Exception as e:  # noqa: BLE001
            return False, str(e)
        if f'"{self.vm}"' not in out.stdout:
            return False, f'VM "{self.vm}" is not running (running: {out.stdout.strip() or "none"})'
        return True, f'VM "{self.vm}" is running'

    def send(self, op):
        codes = tnckeymap.scancodes_for(op)
        if codes is None:
            return f"[vbox] {op!r}: unmapped, nothing sent"
        args = [self.vboxmanage, "controlvm", self.vm, "keyboardputscancode"] + \
               [f"{b:02x}" for b in codes]
        res = subprocess.run(args, capture_output=True, text=True, timeout=10)
        if res.returncode != 0:
            raise TransportError(res.stderr.strip() or f"VBoxManage exit {res.returncode}")
        return f"[vbox] {op!r:12} -> {' '.join(f'{b:02x}' for b in codes)}"


class HeuinputTransport:
    """Write KP/KR Linux keycodes into the guest /tmp/__heuinput FIFO.

    `deliver_cmd` is a shell command that reads the token text on stdin and
    delivers it into the guest, e.g.:
        ssh -p 2222 user@127.0.0.1 'cat >> /tmp/__heuinput'
    """
    name = "heuinput"

    def __init__(self, deliver_cmd):
        self.deliver_cmd = deliver_cmd

    def available(self):
        if not self.deliver_cmd:
            return False, "no --heu-cmd configured"
        return True, f"will pipe tokens to: {self.deliver_cmd}"

    def send(self, op):
        tokens = tnckeymap.heuinput_tokens(op)
        if tokens is None:
            return f"[heuinput] {op!r}: unmapped, nothing sent"
        res = subprocess.run(self.deliver_cmd, shell=True, input=tokens,
                             capture_output=True, text=True, timeout=15)
        if res.returncode != 0:
            raise TransportError(res.stderr.strip() or f"deliver cmd exit {res.returncode}")
        return f"[heuinput] {op!r:12} -> {tokens.strip().replace(chr(10), ' ')}"


def make_transport(kind, vm=None, vboxmanage="VBoxManage", heu_cmd=None):
    if kind == "vbox":
        return VBoxScancodeTransport(vm, vboxmanage)
    if kind == "heuinput":
        return HeuinputTransport(heu_cmd)
    if kind == "dry":
        return DryRunTransport()
    raise ValueError(f"unknown transport {kind!r}")


if __name__ == "__main__":
    # Quick smoke test of the dry transport over a few operations.
    t = DryRunTransport()
    for op in ("CE", "PGMMGT", "SIMU", "7", "ACTPOS", "VSKDOWN"):
        print(t.send(op))
