#!/usr/bin/env bash
#
# build_and_verify_plcbin.sh — decompile->ARM64 recompile of libplcbin (a
# stateful PLC-binary-module file parser), PROVEN equivalent to the proprietary
# i386 .so by differential testing on a crafted .bin (parsed outputs byte-diffed).
#
#   truth   : i386 harness linked against the REAL libplcbin.so, under qemu-i386.
#   rebuilt : native ARM64 harness linked against libplcbin_rebuilt.c, native M2.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libplcbin_arm64.dylib" "$HERE/libplcbin_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_plcbin.c" "$HERE/libplcbin_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libplcbin_aarch64.so "$HERE/libplcbin_rebuilt.c"
limactl copy "$VM":/tmp/libplcbin_aarch64.so "$HERE/libplcbin_aarch64.so"
file "$HERE/libplcbin_arm64.dylib" "$HERE/libplcbin_aarch64.so" | sed "s#$HERE/##"

echo "== make a loadable oracle: trim the REAL .so to its libc+codecheck deps =="
# libplcbin.so's DT_NEEDED drags the whole HeROS runtime (libheros/libGMessage/
# glib/...), whose load-time ctors need the kernel API. The 5 functions under
# test only reference libc + the (light) codecheck Registration ctor, so we copy
# the real proprietary code and remove the irrelevant NEEDED entries — still the
# genuine i386 machine code, just a loadable dependency list.
cp -f "$BIN/libplcbin.so" "$HERE/libplcbin_trim.so"
patchelf --set-soname libplcbin_trim.so "$HERE/libplcbin_trim.so"
for L in libhdhinput.so libglib-2.0.so.0 libxml2.so.2 libfile.so libRecordStream.so \
         libheuseradmin.so.1 libbackend.so libheloglib.so.1 libevents.so libjhminizip.so.1 \
         libz.so.1 libGMessageMisc.so libGMessageConfig.so libGMessageShared.so libgmsglib.so \
         libstrings.so libjhvolume.so.1 libheros.so.1 libcrypt.so.2; do
  patchelf --remove-needed "$L" "$HERE/libplcbin_trim.so" 2>/dev/null || true
done
echo "  trimmed NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libplcbin_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_plcbin.c" \
  -L"$HERE" -L"$BIN" -L"$R/lib" -L"$R/usr/lib" -lplcbin_trim \
  -Wl,-rpath-link,"$BIN" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_plcbin

echo "== run truth under qemu-i386 (combined sysroot: cross glibc + HeROS libstdc++) =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; CTRL="'"$REPO"'/work/control/sysroot"; HERE="'"$HERE"'"; CS=/tmp/cs_plcbin; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libplcbin_trim.so $CS/usr/lib/libplcbin_trim.so
  ln -sf $CTRL/heros5/bin/libcodecheck-registry.so $CS/usr/lib/libcodecheck-registry.so
  ln -sf $CTRL/heros5 $CS/heros5
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/heros5/bin:/usr/lib:/lib /tmp/truth_plcbin > /tmp/truth_plcbin.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_plcbin.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
