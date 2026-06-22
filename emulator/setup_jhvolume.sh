#!/bin/sh
# setup_jhvolume.sh — populate the HeROS volume database /etc/jhvolume so the control's
# libjhvolume can resolve volume paths (SYS:, PLC:, TNC:, …) to real directories.
#
# WHY (blocker #6): ConfigServer reads its config INDEX (jhconfigfiles.cfg, via the direct
# -f= path) but the files listed inside use VOLUME paths, e.g. "SYS:\config\tnc.cfg". Those
# resolve through the HeROS volume manager (libjhvolume), which reads /etc/jhvolume. With no
# /etc/jhvolume the control spins retrying it (strace: open("/etc/jhvolume")=ENOENT loop) and
# never loads the channel-group config (tnc.cfg) → IPO's "NC" lookup returns -1 → IPO aborts
# with "Invalid Command Option -k" (AFTER it has connected — see blocker #5, INJECT_ACK).
#
# STATUS: NECESSARY but NOT YET SUFFICIENT. With this DB present:
#   jhvolume "SYS/config/tnc.cfg"   -> /tmp/s/config/tnc.cfg   (RESOLVES, slash form)
#   jhvolume "SYS:\config\tnc.cfg"  -> SYS:/config/tnc.cfg      (does NOT resolve — colon form)
# The control uses the colon form (VOLUME:\path). That convention resolves in the real system
# via FUSE volume MOUNTS the volume manager sets up (the documented FUSE-backend layer), not by
# plain string substitution. Remaining work: provide those mounts (or make libjhvolume's
# colon-form API resolve), then ConfigServer can load tnc.cfg and serve the "NC" channel group.
#
# Run once in the lima VM (writes /etc/jhvolume, needs root): sh emulator/setup_jhvolume.sh
set -e
R=${R:-/Users/andreansx/Documents/TNC640unix/work/target/rootfs}
JH="$R/usr/sbin/jhvolume"
jhv(){ sudo qemu-i386 -L "$R" -E LD_LIBRARY_PATH=/heros5/bin:/lib:/usr/lib \
        "$R/lib/ld-linux.so.2" "$JH" "$@"; }
sudo rm -f /etc/jhvolume
jhv --set SYS    /tmp/s -a=sys  >/dev/null   # SYS partition  (config, binaries)  -> control sysroot
jhv --set SYSTEM /tmp/s -a=sys  >/dev/null
jhv --set PLC    /tmp/o -a=oem  >/dev/null   # OEM partition
jhv --set PLCE   /tmp/o -a=oem  >/dev/null
jhv --set OEM    /tmp/o -a=oem  >/dev/null
jhv --set TNC    /tmp/s -a=user >/dev/null   # USR partition
echo "== /etc/jhvolume =="; sudo cat /etc/jhvolume
echo "== --list =="; jhv --list
