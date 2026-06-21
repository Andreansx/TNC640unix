#!/usr/bin/env bash
#
# build_and_verify_xmlhash.sh — decompile->ARM64 recompile of the hash + setter
# leaves of libxmlreader, PROVEN byte-identical to the proprietary i386 .so.
#
# Verified subset (3 fns): XmlKeyHashBinary (Jenkins one-at-a-time hash over signed
# bytes), XmlHashSetKey, XmlHashSetValueAllocator (single-level field stores).
#
# Oracle technique (cf. errplib/gtlib): trim heavy DT_NEEDED (incl. libz/minizip),
# satisfy residual imports + the HEROSLIB_500.0 VERNEED with one stub .so (soname
# libheros.so.1), neuter the C++ static ctors.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libxmlreader_partial_arm64.dylib" "$HERE/libxmlreader_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_xmlhash.c" "$HERE/libxmlreader_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libxmlreader_partial_aarch64.so "$HERE/libxmlreader_partial_rebuilt.c"
limactl copy "$VM":/tmp/libxmlreader_partial_aarch64.so "$HERE/libxmlreader_partial_aarch64.so"
file "$HERE/libxmlreader_partial_arm64.dylib" "$HERE/libxmlreader_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386, soname=libheros.so.1) =="
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libxmlStubs.so "$HERE/xml_stub.c" \
  -Wl,--version-script="$HERE/xml_stub.ver" -Wl,-soname,libheros.so.1
limactl copy "$VM":/tmp/libxmlStubs.so "$HERE/libxmlStubs.so"
ln -sf libxmlStubs.so "$HERE/libheros.so.1"

echo "== make a loadable oracle: trim the REAL .so (keep libheros.so.1 -> stub) =="
cp -f "$BIN/libxmlreader.so" "$HERE/libxmlreader_trim.so"
patchelf --set-soname libxmlreader_trim.so "$HERE/libxmlreader_trim.so"
for L in libstrings.so libcodecheck-registry.so libjhminizip.so.1 libz.so.1; do
  patchelf --remove-needed "$L" "$HERE/libxmlreader_trim.so" 2>/dev/null || true
done
python3 "$HERE/neuter_init.py" "$HERE/libxmlreader_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libxmlreader_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_xmlhash.c" \
  -L"$HERE" -lxmlreader_trim -l:libheros.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_xmlhash

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_xml; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libxmlreader_trim.so $CS/usr/lib/libxmlreader_trim.so
  ln -sf $HERE/libxmlStubs.so $CS/usr/lib/libheros.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_xmlhash > /tmp/truth_xmlhash.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_xmlhash.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
