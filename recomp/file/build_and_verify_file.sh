#!/usr/bin/env bash
#
# build_and_verify_file.sh — decompile->ARM64 recompile of the EXPORTED
# pure-integer leaf subset of libfile (HeROS file layer), PROVEN byte-identical
# to the proprietary i386 .so.
#
# Verified subset (5 fns): BitFieldTst (signed bit-array test), IsNcFile/IsAscFile
# (file-type tag predicates), FlServerListSize (header field), read_mminch (const).
#
# Oracle technique (cf. errplib/gtlib): trim heavy DT_NEEDED, satisfy residual
# imports + the HEROSLIB_500.0 VERNEED with one stub .so (soname libheros.so.1),
# neuter the C++ static ctors.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libfile_partial_arm64.dylib" "$HERE/libfile_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_file.c" "$HERE/libfile_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libfile_partial_aarch64.so "$HERE/libfile_partial_rebuilt.c"
limactl copy "$VM":/tmp/libfile_partial_aarch64.so "$HERE/libfile_partial_aarch64.so"
file "$HERE/libfile_partial_arm64.dylib" "$HERE/libfile_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386, soname=libheros.so.1) =="
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libfileStubs.so "$HERE/file_stub.c" \
  -Wl,--version-script="$HERE/file_stub.ver" -Wl,-soname,libheros.so.1
limactl copy "$VM":/tmp/libfileStubs.so "$HERE/libfileStubs.so"
ln -sf libfileStubs.so "$HERE/libheros.so.1"

echo "== make a loadable oracle: trim the REAL .so (keep libheros.so.1 -> stub) =="
cp -f "$BIN/libfile.so" "$HERE/libfile_trim.so"
patchelf --set-soname libfile_trim.so "$HERE/libfile_trim.so"
for L in libRecordStream.so libcodecheck-registry.so libbackend.so libevents.so \
         libheuseradmin.so.1 libgmsglib.so libstrings.so; do
  patchelf --remove-needed "$L" "$HERE/libfile_trim.so" 2>/dev/null || true
done
python3 "$HERE/neuter_init.py" "$HERE/libfile_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libfile_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_file.c" \
  -L"$HERE" -lfile_trim -l:libheros.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_file

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_file; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libfile_trim.so $CS/usr/lib/libfile_trim.so
  ln -sf $HERE/libfileStubs.so $CS/usr/lib/libheros.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_file > /tmp/truth_file.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_file.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
