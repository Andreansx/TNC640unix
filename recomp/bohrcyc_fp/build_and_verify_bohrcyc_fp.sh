#!/usr/bin/env bash
#
# build_and_verify_bohrcyc_fp.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the FLOATING-POINT leaves of libEp90_Bohrcyc
# (BCYC_EntnormiereWinkel, BCYC_WinkelGleich), proven OBSERVABLY EQUIVALENT
# (exact return codes + doubles within ~1 ULP) to the proprietary i386 .so run
# under qemu-i386. These were excluded from the byte-identical set because they
# cross the x87-vs-SSE / transcendental boundary — see README.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Bohrcyc_fp_arm64.dylib" "$HERE/libEp90_Bohrcyc_fp_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_bohrcyc_fp.c" "$HERE/libEp90_Bohrcyc_fp_rebuilt.c" -lm -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Bohrcyc_fp_aarch64.so "$HERE/libEp90_Bohrcyc_fp_rebuilt.c"
limactl copy "$VM":/tmp/libEp90_Bohrcyc_fp_aarch64.so "$HERE/libEp90_Bohrcyc_fp_aarch64.so"
file "$HERE/libEp90_Bohrcyc_fp_arm64.dylib" "$HERE/libEp90_Bohrcyc_fp_aarch64.so" | sed "s#$HERE/##"

echo "== make a loadable oracle: trim the REAL .so to libc+libm =="
# Both functions reference only sincos/fabs (libm); drop the Ep90 geometry deps
# so the genuine i386 .text loads standalone.
cp -f "$BIN/libEp90_Bohrcyc.so" "$HERE/libEp90_Bohrcyc_trim.so"
patchelf --set-soname libEp90_Bohrcyc_trim.so "$HERE/libEp90_Bohrcyc_trim.so"
for L in libEp90_Fflib.so libEp90_Gtlib.so libEp90_Geolib.so libEp90_Errplib.so; do
  patchelf --remove-needed "$L" "$HERE/libEp90_Bohrcyc_trim.so" 2>/dev/null || true
done
echo "  trimmed NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Bohrcyc_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_bohrcyc_fp.c" \
  -L"$HERE" -L"$BIN" -L"$R/lib" -L"$R/usr/lib" -lEp90_Bohrcyc_trim -lm \
  -Wl,-rpath-link,"$BIN" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_bohrcyc_fp

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; CTRL="'"$REPO"'/work/control/sysroot"; HERE="'"$HERE"'"; CS=/tmp/cs_bohrcyc_fp; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Bohrcyc_trim.so $CS/usr/lib/libEp90_Bohrcyc_trim.so
  ln -sf $CTRL/heros5/bin/libcodecheck-registry.so $CS/usr/lib/libcodecheck-registry.so 2>/dev/null || true
  ln -sf $CTRL/heros5 $CS/heros5
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/heros5/bin:/usr/lib:/lib /tmp/truth_bohrcyc_fp > /tmp/truth_bohrcyc_fp.txt
'

echo "== compare (tolerant: exact return codes, doubles within ~1 ULP) =="
limactl copy "$VM":/tmp/truth_bohrcyc_fp.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
python3 "$HERE/compare_fp.py" "$HERE/truth.txt" "$HERE/recomp.txt"
