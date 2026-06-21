#!/usr/bin/env bash
#
# build_and_verify_dintabs.sh — decompile->ARM64 recompile of libEp90_Dintabs's
# pure thread-table functions, PROVEN equivalent to the proprietary i386 .so by
# differential testing (same harness run two ways, outputs byte-compared).
#
#   truth   : i386 harness linked against the REAL libEp90_Dintabs.so, run under
#             qemu-i386 translation inside the ARM64 lima VM (`tnc`).
#   rebuilt : native ARM64 harness linked against libEp90_Dintabs_rebuilt.c,
#             run natively on the M2.
#
# Requires the `tnc` VM (qemu-user + gcc-i686-linux-gnu) and the extracted rootfs.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== (re)extract exact table bytes from the proprietary .so =="
python3 "$HERE/extract_tables.py"

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + verify harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Dintabs_arm64.dylib" "$HERE/libEp90_Dintabs_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_dintabs.c" "$HERE/libEp90_Dintabs_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Dintabs_aarch64.so "$HERE/libEp90_Dintabs_rebuilt.c"
limactl copy "$VM":/tmp/libEp90_Dintabs_aarch64.so "$HERE/libEp90_Dintabs_aarch64.so"
file "$HERE/libEp90_Dintabs_arm64.dylib" "$HERE/libEp90_Dintabs_aarch64.so" | sed "s#$HERE/##"

echo "== i386 ground-truth build against the REAL proprietary .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_dintabs.c" \
  -L"$BIN" -L"$R/lib" -L"$R/usr/lib" -lEp90_Dintabs \
  -Wl,-rpath-link,"$BIN" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_dintabs

echo "== run truth under qemu-i386 (combined sysroot: cross glibc + HeROS libstdc++) =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; CTRL="'"$REPO"'/work/control/sysroot"; CS=/tmp/cs_dintabs; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $CTRL/heros5 $CS/heros5
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/heros5/bin:/usr/lib:/lib /tmp/truth_dintabs > /tmp/truth_dintabs.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_dintabs.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH (first 20 diffs)"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -20; exit 1
fi
