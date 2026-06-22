#!/bin/bash
# capture_render.sh — run AppStartMP under FEX and SCREENSHOT Xvfb synced to the moment AppStartMP
# connects to X (so we catch the HEIDENHAIN boot logo / whatever the LogoModule draws), then copy the
# richest frame out to work/render_capture/ on the Mac. Wraps run_appstart_fex.sh's environment.
# Run in VM tnc:  limactl shell tnc -- bash <repo>/emulator/capture_render.sh
set -u
OUT=/Users/andreansx/Documents/TNC640unix/work/render_capture
mkdir -p "$OUT"; rm -f "$OUT"/live_*.png 2>/dev/null
DISP=:99

# Background capturer: wait until AppStartMP's strace shows the X99 connect, then grab frames fast.
(
  for i in $(seq 1 240); do grep -aq "X11-unix/X99" /tmp/a_strace.log 2>/dev/null && break; sleep 1; done
  echo "[cap] AppStartMP connected to X — capturing 20 frames @1.5s"
  for n in $(seq -w 1 20); do
    if DISPLAY=$DISP xwd -root -silent > /tmp/live_$n.xwd 2>/dev/null; then
      convert /tmp/live_$n.xwd "$OUT/live_$n.png" 2>/dev/null && rm -f /tmp/live_$n.xwd
      printf "[cap] live_%s.png : %s colors\n" "$n" "$(convert "$OUT/live_$n.png" -format '%k' info: 2>/dev/null)"
    fi
    sleep 1.5
  done
) &
CAPPID=$!

# Run the standard AppStartMP harness (starts Xvfb :99 + the constellation prereqs + AppStartMP).
bash /Users/andreansx/Documents/TNC640unix/emulator/run_appstart_fex.sh
kill $CAPPID 2>/dev/null

echo "=== richest frames ==="
for f in "$OUT"/live_*.png; do [ -e "$f" ] && printf "%s %s\n" "$(convert "$f" -format '%k' info: 2>/dev/null)" "$f"; done | sort -rn | head -5
