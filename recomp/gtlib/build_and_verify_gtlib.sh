#!/usr/bin/env bash
#
# build_and_verify_gtlib.sh — decompile->ARM64 recompile of the single-level
# GTFIND_Is* classifiers of libEp90_Gtlib (EP90 geometry/Geotec library),
# PROVEN byte-identical to the proprietary i386 .so.
#
# Verified subset (7 fns, C++ mangled symbols): IsBohrung/IsFasRun/IsFreistich/
# IsEinstich/IsGewinde (clean bool, read geotec tag @+0x54), IsVariante and
# IsFigurRucksack (with the i386 return-register leak). The list-walking and FP
# geometry functions are NOT leaves.
#
# Oracle technique (cf. errplib): trim the heavy DT_NEEDED, satisfy the residual
# load-time relocations + the HEROSLIB_500.0 VERNEED with one stub .so whose
# soname is libheros.so.1, and disable the C++ static ctors (neuter_init.py).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Gtlib_partial_arm64.dylib" "$HERE/libEp90_Gtlib_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_gtlib.c" "$HERE/libEp90_Gtlib_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Gtlib_partial_aarch64.so "$HERE/libEp90_Gtlib_partial_rebuilt.c"
limactl copy "$VM":/tmp/libEp90_Gtlib_partial_aarch64.so "$HERE/libEp90_Gtlib_partial_aarch64.so"
file "$HERE/libEp90_Gtlib_partial_arm64.dylib" "$HERE/libEp90_Gtlib_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386, soname=libheros.so.1) =="
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libGtlibStubs.so "$HERE/gtlib_stub.c" \
  -Wl,--version-script="$HERE/gtlib_stub.ver" -Wl,-soname,libheros.so.1
limactl copy "$VM":/tmp/libGtlibStubs.so "$HERE/libGtlibStubs.so"
ln -sf libGtlibStubs.so "$HERE/libheros.so.1"

echo "== make a loadable oracle: trim the REAL .so (keep libheros.so.1 -> stub) =="
cp -f "$BIN/libEp90_Gtlib.so" "$HERE/libEp90_Gtlib_trim.so"
patchelf --set-soname libEp90_Gtlib_trim.so "$HERE/libEp90_Gtlib_trim.so"
for L in libEp90_Geolib.so libGMessageGeo.so libProductId.so libcodecheck-registry.so \
         libEp90_Errplib.so libSharedMemLib.so libRecordStream.so libbackend.so libGMessageGui.so \
         libevents.so libGMessageMisc.so libGMessageConfig.so libGMessageShared.so libgmsglib.so libstrings.so; do
  patchelf --remove-needed "$L" "$HERE/libEp90_Gtlib_trim.so" 2>/dev/null || true
done
python3 "$HERE/neuter_init.py" "$HERE/libEp90_Gtlib_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Gtlib_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_gtlib.c" \
  -L"$HERE" -lEp90_Gtlib_trim -l:libheros.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_gtlib

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_gtlib; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Gtlib_trim.so $CS/usr/lib/libEp90_Gtlib_trim.so
  ln -sf $HERE/libGtlibStubs.so $CS/usr/lib/libheros.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_gtlib > /tmp/truth_gtlib.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_gtlib.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
