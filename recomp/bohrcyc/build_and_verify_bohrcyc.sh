#!/usr/bin/env bash
#
# build_and_verify_bohrcyc.sh — decompile->ARM64 recompile of the PURE-INTEGER
# leaf subset of libEp90_Bohrcyc, PROVEN equivalent to the proprietary i386 .so.
# (The lib as a whole is NOT a leaf — FP geometry + Ep90 deps; only the integer
#  accessors are byte-identical-verifiable. See README.)
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Bohrcyc_partial_arm64.dylib" "$HERE/libEp90_Bohrcyc_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_bohrcyc.c" "$HERE/libEp90_Bohrcyc_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Bohrcyc_partial_aarch64.so "$HERE/libEp90_Bohrcyc_partial_rebuilt.c"
limactl copy "$VM":/tmp/libEp90_Bohrcyc_partial_aarch64.so "$HERE/libEp90_Bohrcyc_partial_aarch64.so"
file "$HERE/libEp90_Bohrcyc_partial_arm64.dylib" "$HERE/libEp90_Bohrcyc_partial_aarch64.so" | sed "s#$HERE/##"

echo "== make a loadable oracle: trim the REAL .so to libc+codecheck =="
# The two functions reference NO external symbols, so we can drop the Ep90
# geometry deps and load the genuine i386 code standalone.
cp -f "$BIN/libEp90_Bohrcyc.so" "$HERE/libEp90_Bohrcyc_trim.so"
patchelf --set-soname libEp90_Bohrcyc_trim.so "$HERE/libEp90_Bohrcyc_trim.so"
for L in libEp90_Fflib.so libEp90_Gtlib.so libEp90_Geolib.so libEp90_Errplib.so; do
  patchelf --remove-needed "$L" "$HERE/libEp90_Bohrcyc_trim.so" 2>/dev/null || true
done
echo "  trimmed NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Bohrcyc_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_bohrcyc.c" \
  -L"$HERE" -L"$BIN" -L"$R/lib" -L"$R/usr/lib" -lEp90_Bohrcyc_trim \
  -Wl,-rpath-link,"$BIN" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_bohrcyc

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; CTRL="'"$REPO"'/work/control/sysroot"; HERE="'"$HERE"'"; CS=/tmp/cs_bohrcyc; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Bohrcyc_trim.so $CS/usr/lib/libEp90_Bohrcyc_trim.so
  ln -sf $CTRL/heros5/bin/libcodecheck-registry.so $CS/usr/lib/libcodecheck-registry.so
  ln -sf $CTRL/heros5 $CS/heros5
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/heros5/bin:/usr/lib:/lib /tmp/truth_bohrcyc > /tmp/truth_bohrcyc.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_bohrcyc.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -30; exit 1
fi
