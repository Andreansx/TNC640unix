#!/usr/bin/env bash
# Generate the DIAGNOSTIC layout that makes the winmgr render-thread SIGSEGV
# DETERMINISTIC. The stock resource/tnc640layout1280.xml defines SCREEN_OEM as a
# <desktop> (winmgr creates it ON-DEMAND when an OEM app registers), so the crash
# only fires once Guppy drives far enough to request the OEM screen (intermittent).
# Flipping SCREEN_OEM to a <screen> makes winmgr create it at STARTUP → it crashes
# EVERY run with the documented context: a winmgr sub-thread does
#   P_signal(0xffffffff,0x02000000) -> P_name(buf,-1) -> T_name(buf+0x11,-1) -> SIGSEGV
# i.e. the WmClient ctor for a pid=-1 client during OEM-screen creation.
#
# Usage:  bash emulator/make_oemscr_layout.sh
# Then:   WM_SEGVBT=2 PIDENT_SELF=1 SK_REPLY_FORCE=1 WINMGR=1 AREA_RECT_FORCE=1 \
#         EMPTYPOLL_YIELD=300 WMQ_BREAK=1 WMQ_BREAK_N=64 \
#         WM_LAYOUT=%SYS%/resource/tnc640layout1280_oemscr.xml WM_SIZE=1280x1024 HWV_FORCE_FS=1 \
#         limactl shell tnc -- bash -s < emulator/run_3proc_skmgr_guppy.sh
set -eu
REPO=/Users/andreansx/Documents/TNC640unix
RES="$REPO/work/control/sysroot/resource"
SRC="$RES/tnc640layout1280.xml"
DST="$RES/tnc640layout1280_oemscr.xml"
[ -f "$SRC" ] || { echo "missing $SRC" >&2; exit 1; }
python3 - "$SRC" "$DST" <<'PY'
import sys
src, dst = sys.argv[1], sys.argv[2]
s = open(src, encoding='latin-1').read()
old = '<desktop screenId="SCREEN_OEM"       desktopId="2" name="OEM" title="OEM"/>'
new = ('<screen  screenId="SCREEN_OEM"       desktopId="2" name="OEM" title="OEM" '
       'iconSmall="%SYS%\\resource\\ico_machine16.bmp" iconLarge="%SYS%\\resource\\ico_machine.bmp"/>')
assert old in s, "SCREEN_OEM <desktop> line not found (layout changed?)"
open(dst, 'w', encoding='latin-1').write(s.replace(old, new))
print("wrote", dst)
PY
