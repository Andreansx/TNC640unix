#!/usr/bin/env bash
#
# build_and_verify_plccond.sh — decompile->ARM64 recompile of the PURE-INTEGER
# leaf subset of libplccond (PLC condition-expression evaluator), PROVEN
# byte-identical to the proprietary i386 .so.
#
# Verified subset (8 fns): ASCII case folding (toupper_ASCII, tolower_ASCII),
# path-separator test (IsPathSep), operand null-scan (isNull), and the
# fixed-capacity uint16 operand stack over a caller flat buffer
# (IsStackEmpty, PeekStack, PushStack, PopStack). The condition parser itself
# (heap/config/message-bus) is NOT a leaf.
#
# Oracle technique (cf. errplib): trim the heavy DT_NEEDED, satisfy the residual
# load-time relocations + the JHVOLUMELIB_500.0 VERNEED with one stub .so whose
# soname is libjhvolume.so.1, and disable the C++ static ctors (neuter_init.py).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libplccond_partial_arm64.dylib" "$HERE/libplccond_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_plccond.c" "$HERE/libplccond_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libplccond_partial_aarch64.so "$HERE/libplccond_partial_rebuilt.c"
limactl copy "$VM":/tmp/libplccond_partial_aarch64.so "$HERE/libplccond_partial_aarch64.so"
file "$HERE/libplccond_partial_arm64.dylib" "$HERE/libplccond_partial_aarch64.so" | sed "s#$HERE/##"

echo "== build the load-satisfying stub .so (i386, soname=libjhvolume.so.1) =="
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
  -o /tmp/libplccondStubs.so "$HERE/plccond_stub.c" \
  -Wl,--version-script="$HERE/plccond_stub.ver" -Wl,-soname,libjhvolume.so.1
limactl copy "$VM":/tmp/libplccondStubs.so "$HERE/libplccondStubs.so"
ln -sf libplccondStubs.so "$HERE/libjhvolume.so.1"

echo "== make a loadable oracle: trim the REAL .so (keep libjhvolume.so.1 -> stub) =="
cp -f "$BIN/libplccond.so" "$HERE/libplccond_trim.so"
patchelf --set-soname libplccond_trim.so "$HERE/libplccond_trim.so"
for L in libheros.so.1 libcodecheck-registry.so; do
  patchelf --remove-needed "$L" "$HERE/libplccond_trim.so" 2>/dev/null || true
done
python3 "$HERE/neuter_init.py" "$HERE/libplccond_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libplccond_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_plccond.c" \
  -L"$HERE" -lplccond_trim -l:libjhvolume.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_plccond

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_plccond; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libplccond_trim.so $CS/usr/lib/libplccond_trim.so
  ln -sf $HERE/libplccondStubs.so $CS/usr/lib/libjhvolume.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_plccond > /tmp/truth_plccond.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_plccond.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
