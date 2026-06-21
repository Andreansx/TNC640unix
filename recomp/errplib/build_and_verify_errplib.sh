#!/usr/bin/env bash
#
# build_and_verify_errplib.sh — decompile->ARM64 recompile of the PURE-LEAF
# subset of libEp90_Errplib (EP90 error-handling lib), PROVEN byte-identical
# to the proprietary i386 .so.
#
# Verified subset (12 fns): the error-code classifiers (ERR_Is*), the
# facility-ID table lookup (ERRPLIB_GetFacilityID, 72-entry .rodata table
# lifted verbatim), and the constant predicate IsDPDemo. The rest of the lib
# (string building, shared-memory, HeROS message bus) is NOT a leaf.
#
# Oracle technique (cf. plcbin/bohrcyc): the real .so drags the whole HeROS
# runtime via DT_NEEDED + a HEROSLIB_500.0 version requirement, but the 12 leaf
# functions reference NO external symbols. So we load the genuine proprietary
# .text standalone by (a) trimming the heavy NEEDED, (b) satisfying the
# remaining load-time relocations/versions with an auto-generated stub .so whose
# symbols the leaf functions never touch.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Errplib_partial_arm64.dylib" "$HERE/libEp90_Errplib_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_errplib.c" "$HERE/libEp90_Errplib_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Errplib_partial_aarch64.so "$HERE/libEp90_Errplib_partial_rebuilt.c"
limactl copy "$VM":/tmp/libEp90_Errplib_partial_aarch64.so "$HERE/libEp90_Errplib_partial_aarch64.so"
file "$HERE/libEp90_Errplib_partial_arm64.dylib" "$HERE/libEp90_Errplib_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386, soname=libheros.so.1) =="
# The trimmed .so keeps a VERNEED on HEROSLIB_500.0 keyed to file 'libheros.so.1',
# so the stub must carry that soname AND provide that version, plus every other
# non-glibc symbol the real .text references via load-time relocations.
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libErrplibStubs.so "$HERE/errplib_stub.c" \
  -Wl,--version-script="$HERE/errplib_stub.ver" -Wl,-soname,libheros.so.1
limactl copy "$VM":/tmp/libErrplibStubs.so "$HERE/libErrplibStubs.so"
ln -sf libErrplibStubs.so "$HERE/libheros.so.1"   # link/run name must equal the NEEDED string

echo "== make a loadable oracle: trim the REAL .so (keep libheros.so.1 -> stub) =="
cp -f "$BIN/libEp90_Errplib.so" "$HERE/libEp90_Errplib_trim.so"
patchelf --set-soname libEp90_Errplib_trim.so "$HERE/libEp90_Errplib_trim.so"
for L in libSharedMemLib.so libbackend-server.so libGetPath.so libcodecheck-registry.so \
         libRecordStream.so libbackend.so libevents.so libGMessageMisc.so libGMessageConfig.so \
         libGMessageShared.so libgmsglib.so libstrings.so; do
  patchelf --remove-needed "$L" "$HERE/libEp90_Errplib_trim.so" 2>/dev/null || true
done
# Disable the library's C++ static ctors/dtors: the leaf functions need no global
# init, and the ctors would otherwise call into the trimmed-away HeROS runtime.
python3 "$HERE/neuter_init.py" "$HERE/libEp90_Errplib_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Errplib_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_errplib.c" \
  -L"$HERE" -lEp90_Errplib_trim -l:libheros.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_errplib

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_errplib; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Errplib_trim.so $CS/usr/lib/libEp90_Errplib_trim.so
  ln -sf $HERE/libErrplibStubs.so $CS/usr/lib/libheros.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_errplib > /tmp/truth_errplib.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_errplib.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
