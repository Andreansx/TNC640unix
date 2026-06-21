#!/usr/bin/env bash
#
# build_and_verify_productid.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the leaf C++ class methods of libProductId (product /
# control-mark identity predicates), proven OBSERVABLY EQUIVALENT to the
# proprietary i386 .so under qemu-i386. Output is deterministic (bool predicates
# read as their ABI-defined low byte), so the proof is a plain diff.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libProductId.so"

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libProductId_arm64.dylib" "$HERE/libProductId_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_productid.c" "$HERE/libProductId_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libProductId_aarch64.so "$HERE/libProductId_rebuilt.c"
limactl copy "$VM":/tmp/libProductId_aarch64.so "$HERE/libProductId_aarch64.so"
file "$HERE/libProductId_arm64.dylib" "$HERE/libProductId_aarch64.so" | sed "s#$HERE/##"

echo "== generate stub for unversioned UND symbols (HeROS-runtime refs) =="
limactl shell "$VM" -- bash -c "i686-linux-gnu-nm -D --undefined-only '$SRC' 2>/dev/null" > "$HERE/_und.txt"
python3 - "$HERE/_und.txt" "$HERE/productid_stub.c" <<'PY'
import sys
syms=[]
for ln in open(sys.argv[1]):
    p=ln.split()
    if not p: continue
    typ=p[0] if len(p)>1 else None
    name=p[-1]
    if '@' in name: continue
    if typ not in ('U',): continue
    syms.append(name)
syms=sorted(set(syms))
with open(sys.argv[2],"w") as f:
    f.write("/* auto-generated: define unversioned UND symbols for load-time relocs */\n")
    for s in syms: f.write(f"void {s}(void){{}}\n")
print(f"  stubbed {len(syms)} unversioned UND symbols")
PY
rm -f "$HERE/_und.txt"
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/libProductIdStubs.so "$HERE/productid_stub.c" -Wl,-soname,libProductIdStubs.so
limactl copy "$VM":/tmp/libProductIdStubs.so "$HERE/libProductIdStubs.so"

echo "== make a loadable oracle: trim heavy NEEDED, add stub, neuter ctors =="
cp -f "$SRC" "$HERE/libProductId_trim.so"
PE_ARGS=(--set-soname libProductId_trim.so)
for dep in $(limactl shell "$VM" -- readelf -d "$SRC" 2>/dev/null | awk -F'[][]' '/NEEDED/{print $2}'); do
  case "$dep" in
    libstdc++.so.6|libm.so.6|libgcc_s.so.1|libc.so.6) ;;
    *) PE_ARGS+=(--remove-needed "$dep") ;;
  esac
done
PE_ARGS+=(--add-needed libProductIdStubs.so)
patchelf "${PE_ARGS[@]}" "$HERE/libProductId_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libProductId_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libProductId_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_productid.c" \
  -L"$HERE" -lProductId_trim -l:libProductIdStubs.so \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_productid

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_productid; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libProductId_trim.so $CS/usr/lib/libProductId_trim.so
  ln -sf $HERE/libProductIdStubs.so $CS/usr/lib/libProductIdStubs.so
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_productid > /tmp/truth_productid.txt
'

echo "== compare (deterministic -> exact diff) =="
limactl copy "$VM":/tmp/truth_productid.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
