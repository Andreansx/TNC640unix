#!/usr/bin/env bash
#
# build_and_verify.sh — decompile->ARM64 recompile, PROVEN equivalent.
#
# Builds the native ARM64 re-implementation of libhdhinput's numeric accessors
# and proves it is behaviourally identical to the original proprietary i386
# libhdhinput.so by differential testing: the SAME harness is run two ways and
# the outputs are compared byte-for-byte.
#
#   truth   : i386 harness linked against the REAL libhdhinput.so, executed
#             under qemu-i386 translation inside the ARM64 lima VM (`tnc`).
#   rebuilt : native ARM64 harness linked against libhdhinput_rebuilt.c,
#             executed natively on the M2.
#
# Requires: the ARM64 lima VM `tnc` from scripts/arm64_translate_poc.sh, with
# qemu-user + gcc-i686-linux-gnu installed, and the control rootfs extracted.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + verify harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libhdhinput_arm64.dylib" "$HERE/libhdhinput_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_harness.c" "$HERE/libhdhinput_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libhdhinput_aarch64.so "$HERE/libhdhinput_rebuilt.c"
limactl copy "$VM":/tmp/libhdhinput_aarch64.so "$HERE/libhdhinput_aarch64.so"
file "$HERE/libhdhinput_arm64.dylib" "$HERE/libhdhinput_aarch64.so" | sed "s#$HERE/##"

echo "== i386 ground-truth build against the REAL proprietary .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_harness.c" \
  -L"$BIN" -L"$R/lib" -L"$R/usr/lib" -lhdhinput \
  -Wl,-rpath-link,"$BIN" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_i386

echo "== run truth under qemu-i386 (combined sysroot: cross glibc + HeROS libstdc++) =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; CTRL="'"$REPO"'/work/control/sysroot"; CS=/tmp/cs; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $CTRL/heros5 $CS/heros5
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/heros5/bin:/usr/lib:/lib /tmp/truth_i386 > /tmp/truth.txt
'

echo "== compare (4000 differential cases) =="
limactl copy "$VM":/tmp/truth.txt "$HERE/truth.txt"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL ✓  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head; exit 1
fi
