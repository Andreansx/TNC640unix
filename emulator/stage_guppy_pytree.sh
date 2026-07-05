#!/usr/bin/env bash
# stage_guppy_pytree.sh — RUN ON THE MAC. Stage Guppy's Python OEM runtime into
# VM-local mirrors, bypassing the flaky lima virtiofs (which returns the correct
# SIZE but CORRUPT/blank CONTENT for some files under load — verified: pyjh.py and
# hwviewer.mo came across as garbage via `tar`/`cp` over virtiofs, tripping
# `AttributeError: module has no attribute require` (pyjh) and `IOError: Bad magic
# number` (gettext .mo) inside HwViewer.py -> the OEM softkey script never ran).
#
# The reliable channel is rsync over lima's SSH (reads the Mac-native files, which
# individually read fine; the SSH transport is checksummed). run_3proc_skmgr_guppy.sh
# then copies these VM-local mirrors onto tmpfs (RAM, EIO-immune) at run time.
#
# Mirrors (VM-local, on vda1 ext4):
#   /var/tmp/pytree     <- work/control/sysroot/Python           (HwViewer.py, .mo, glade, images)
#   /var/tmp/pytree-sp  <- work/control/sysroot/usr/lib/python/site-packages  (pyjh.py + jh-*)
#
# Usage (on the Mac, from the repo root):  bash emulator/stage_guppy_pytree.sh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
CFG="$REPO/work/control/sysroot"
SSHCFG="$HOME/.lima/tnc/ssh.config"
[ -f "$SSHCFG" ] || { echo "no lima ssh.config at $SSHCFG (is the 'tnc' VM up?)"; exit 1; }

echo "== rsync Python tree -> tnc:/var/tmp/pytree =="
rsync -a --delete --no-perms --no-owner --no-group -e "ssh -F $SSHCFG" \
  "$CFG/Python/" lima-tnc:/tmp/pytree_stage/
echo "== rsync site-packages -> tnc:/var/tmp/pytree-sp =="
rsync -a --delete --no-perms --no-owner --no-group -e "ssh -F $SSHCFG" \
  "$CFG/usr/lib/python/site-packages/" lima-tnc:/tmp/pysp_stage/

# relocate into the /var/tmp mirrors (root-owned, where the run script reads them)
limactl shell tnc -- bash -c '
  sudo rm -rf /var/tmp/pytree /var/tmp/pytree-sp
  sudo cp -a /tmp/pytree_stage /var/tmp/pytree
  sudo cp -a /tmp/pysp_stage   /var/tmp/pytree-sp
  echo "  HwViewer.py md5: $(md5sum /var/tmp/pytree/HwViewer/HwViewer.py 2>/dev/null | cut -d\  -f1)"
  echo "  pyjh.py require:  $(grep -c "def require" /var/tmp/pytree-sp/pyjh.py 2>/dev/null) (must be 1)"
  echo "  hwviewer.mo md5:  $(md5sum /var/tmp/pytree/text/en/LC_MESSAGES/hwviewer.mo 2>/dev/null | cut -d\  -f1)"
'
echo "== staged. run_3proc_skmgr_guppy.sh will tmpfs-copy these mirrors at run time =="
