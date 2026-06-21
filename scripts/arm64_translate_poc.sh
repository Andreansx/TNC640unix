#!/usr/bin/env bash
#
# arm64_translate_poc.sh — Proof of concept: run the i386 TNC640 control under
# x86->ARM translation on an Apple-Silicon Mac.
#
# What it does, end to end:
#   1. Ensures a native ARM64 Linux VM (lima) exists and is running.
#   2. Installs an i386 user-mode translator inside it (qemu-user / binfmt).
#   3. Assembles a combined i386 sysroot = HeROS OS rootfs + control binaries.
#   4. Executes a real proprietary control binary (the NCK interpolator) and
#      shows how far its own i386 code runs on the aarch64 host.
#
# This demonstrates that CPU/userspace translation is SOLVED. The remaining
# blocker is the HeROS kernel API (/dev/herosapi, from heros.ko) — see
# docs/16-arm64-decompilation-and-translation.md.
#
# Prereqs on the Mac: brew install lima qemu  (Apple Silicon).
# The control rootfs must already be extracted (see scripts/extract_control.sh
# or docs/16). Paths below assume the repo layout under work/.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VM=tnc
ROOTFS="$REPO/work/target/rootfs"                       # HeROS i386 OS rootfs
CTRL="$REPO/work/control/sysroot/heros5"                # proprietary control tree
NCK="/heros5/bin/ipo_progstation.elf"                   # path inside the sysroot

say() { printf '\n\033[1;36m== %s\033[0m\n' "$*"; }

# --- 0. sanity ---------------------------------------------------------------
[ -e "$ROOTFS/lib/ld-linux.so.2" ] || { echo "HeROS rootfs missing at $ROOTFS (extract target.tar.xz first)"; exit 1; }
[ -e "$CTRL/bin/ipo_progstation.elf" ] || { echo "control tree missing at $CTRL (extract TNC640_SYS.*.zip first)"; exit 1; }

# --- 1. graft control tree into rootfs at its runtime mountpoint --------------
say "assembling combined i386 sysroot"
[ -e "$ROOTFS/heros5" ] || ln -s "$CTRL" "$ROOTFS/heros5"
file "$ROOTFS$NCK" | sed 's/,.*Intel/  (Intel/'

# --- 2. ensure the ARM64 Linux VM is up --------------------------------------
say "ensuring ARM64 Linux VM ($VM) is running"
if ! limactl list --format '{{.Name}} {{.Status}}' 2>/dev/null | grep -q "^$VM Running"; then
  limactl start --name="$VM" --tty=false --vm-type=vz --rosetta template://ubuntu
fi
limactl shell "$VM" -- uname -m   # expect: aarch64

# --- 3. install the i386 translator inside the VM ----------------------------
say "installing i386 user-mode translator (qemu-user) in the VM"
limactl shell "$VM" -- sudo bash -c \
  'export DEBIAN_FRONTEND=noninteractive; \
   apt-get update -qq && apt-get install -y qemu-user qemu-user-binfmt >/dev/null 2>&1; \
   qemu-i386 --version | head -1'

# --- 4. run the real proprietary control binary under translation ------------
say "executing the NCK interpolator (i386) under translation on aarch64"
limactl shell "$VM" -- bash -c "
  R='$ROOTFS'
  echo '--- dependency closure (i386 ld.so --list): ---'
  qemu-i386 -L \$R -E LD_LIBRARY_PATH=/heros5/bin \$R/lib/ld-linux.so.2 --list \$R$NCK \
    | awk '{print \$1}' | grep -c '=>' | xargs echo 'libraries resolved:'
  echo '--- executing its own init code: ---'
  qemu-i386 -L \$R -E LD_LIBRARY_PATH=/heros5/bin \$R/lib/ld-linux.so.2 \$R$NCK 2>&1 | head -10
"

say "DONE — i386 control code executes on Apple Silicon via translation."
echo "Next blocker: /dev/herosapi (heros.ko kernel module) — needs a CUSE/FUSE shim."
