#!/usr/bin/env bash
#
# build_and_verify_plcmap.sh — decompile->ARM64 recompile of the PURE-INTEGER
# leaf subset of libplcmap (PLC I/O symbol map), PROVEN byte-identical to the
# proprietary i386 .so.
#
# Verified subset (4 EXPORTED fns): Swap_d/Swap_w (endian), UQuadCompare (unsigned
# 64-bit compare), NumberOfCharacters (signed-decimal width, incl. the i386 INT_MIN
# quirk). hexbyte/pmap_* are also leaves but local (not .dynsym) -> not linkable.
#
# Oracle technique (cf. errplib/plccond): trim heavy DT_NEEDED, satisfy residual
# imports + the JHVOLUMELIB_500.0 VERNEED with one stub .so (soname libjhvolume.so.1),
# neuter the C++ static ctors.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libplcmap_partial_arm64.dylib" "$HERE/libplcmap_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_plcmap.c" "$HERE/libplcmap_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libplcmap_partial_aarch64.so "$HERE/libplcmap_partial_rebuilt.c"
limactl copy "$VM":/tmp/libplcmap_partial_aarch64.so "$HERE/libplcmap_partial_aarch64.so"
file "$HERE/libplcmap_partial_arm64.dylib" "$HERE/libplcmap_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386, soname=libjhvolume.so.1) =="
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libplcmapStubs.so "$HERE/plcmap_stub.c" \
  -Wl,--version-script="$HERE/plcmap_stub.ver" -Wl,-soname,libjhvolume.so.1
limactl copy "$VM":/tmp/libplcmapStubs.so "$HERE/libplcmapStubs.so"
ln -sf libplcmapStubs.so "$HERE/libjhvolume.so.1"

echo "== make a loadable oracle: trim the REAL .so (keep libjhvolume.so.1 -> stub) =="
cp -f "$BIN/libplcmap.so" "$HERE/libplcmap_trim.so"
patchelf --set-soname libplcmap_trim.so "$HERE/libplcmap_trim.so"
patchelf --remove-needed libcodecheck-registry.so "$HERE/libplcmap_trim.so" 2>/dev/null || true
python3 "$HERE/neuter_init.py" "$HERE/libplcmap_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libplcmap_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_plcmap.c" \
  -L"$HERE" -lplcmap_trim -l:libjhvolume.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_plcmap

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_plcmap; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libplcmap_trim.so $CS/usr/lib/libplcmap_trim.so
  ln -sf $HERE/libplcmapStubs.so $CS/usr/lib/libjhvolume.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_plcmap > /tmp/truth_plcmap.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_plcmap.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
