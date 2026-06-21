#!/usr/bin/env bash
#
# build_and_verify_wznorm.sh — decompile->ARM64 recompile of the PURE-INTEGER
# leaf subset of libEp90_Wznorm (EP90 tool-normalisation lib), PROVEN
# byte-identical to the proprietary i386 .so.
#
# Verified subset (5 fns): the tool-type codec (GeotecToIntWkzTyp,
# IntToGeotecWkzTyp, AsciiToGeotecWkzTyp) and the integer tool-class
# classifiers (WerkzeugTyp, WZ_IsAussenWkz). The rest of the lib is FP geometry.
#
# Oracle technique (cf. errplib): trim the heavy Ep90/codecheck DT_NEEDED, satisfy
# the remaining load-time relocations with an auto-generated stub .so, and disable
# the library's C++ static ctors (neuter_init.py) — the leaf functions need none
# of that. The .text of the 5 functions is the genuine proprietary code.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Wznorm_partial_arm64.dylib" "$HERE/libEp90_Wznorm_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_wznorm.c" "$HERE/libEp90_Wznorm_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Wznorm_partial_aarch64.so "$HERE/libEp90_Wznorm_partial_rebuilt.c"
limactl copy "$VM":/tmp/libEp90_Wznorm_partial_aarch64.so "$HERE/libEp90_Wznorm_partial_aarch64.so"
file "$HERE/libEp90_Wznorm_partial_arm64.dylib" "$HERE/libEp90_Wznorm_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386) =="
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libWznormStubs.so "$HERE/wznorm_stub.c" -Wl,-soname,libWznormStubs.so
limactl copy "$VM":/tmp/libWznormStubs.so "$HERE/libWznormStubs.so"

echo "== make a loadable oracle: trim the REAL .so + redirect deps to the stub =="
cp -f "$BIN/libEp90_Wznorm.so" "$HERE/libEp90_Wznorm_trim.so"
patchelf --set-soname libEp90_Wznorm_trim.so "$HERE/libEp90_Wznorm_trim.so"
for L in libEp90_Aeplib.so libEp90_Gtlib.so libcodecheck-registry.so libEp90_Geolib.so libEp90_Errplib.so; do
  patchelf --remove-needed "$L" "$HERE/libEp90_Wznorm_trim.so" 2>/dev/null || true
done
patchelf --add-needed libWznormStubs.so "$HERE/libEp90_Wznorm_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libEp90_Wznorm_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Wznorm_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_wznorm.c" \
  -L"$HERE" -lEp90_Wznorm_trim -lWznormStubs \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_wznorm

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_wznorm; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Wznorm_trim.so $CS/usr/lib/libEp90_Wznorm_trim.so
  ln -sf $HERE/libWznormStubs.so $CS/usr/lib/libWznormStubs.so
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_wznorm > /tmp/truth_wznorm.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_wznorm.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
