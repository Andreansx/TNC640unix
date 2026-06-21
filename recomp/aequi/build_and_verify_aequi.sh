#!/usr/bin/env bash
#
# build_and_verify_aequi.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the coordinate-type classifier leaves of libEp90_Aequi
# (IsPolareLaenge, IsCartInkrement, IsPolarerWinkel — flat geotec flag reads),
# proven OBSERVABLY EQUIVALENT to the proprietary i386 .so under qemu-i386.
# No proprietary VERNEED -> metaval-style trim + auto-stub + neuter oracle.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libEp90_Aequi.so"
RB=libEp90_Aequi_rebuilt.c

echo "== native ARM64 build =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Aequi_arm64.dylib" "$HERE/$RB"
clang -arch arm64 -O2 "$HERE/verify_aequi.c" "$HERE/$RB" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Aequi_aarch64.so "$HERE/$RB"
limactl copy "$VM":/tmp/libEp90_Aequi_aarch64.so "$HERE/libEp90_Aequi_aarch64.so"
file "$HERE/libEp90_Aequi_arm64.dylib" "$HERE/libEp90_Aequi_aarch64.so" | sed "s#$HERE/##"

echo "== generate stub for unversioned UND symbols =="
limactl shell "$VM" -- bash -c "i686-linux-gnu-nm -D --undefined-only '$SRC' 2>/dev/null" > "$HERE/_und.txt"
python3 - "$HERE/_und.txt" "$HERE/aequi_stub.c" <<'PY'
import sys
syms=[]
for ln in open(sys.argv[1]):
    p=ln.split()
    if len(p)<2: continue
    typ, name = p[0], p[-1]
    if '@' in name: continue
    if typ != 'U': continue
    syms.append(name)
syms=sorted(set(syms))
with open(sys.argv[2],"w") as f:
    f.write("/* auto-generated: unversioned UND refs for load-time relocs */\n")
    for s in syms: f.write(f"void {s}(void){{}}\n")
print(f"  stubbed {len(syms)} unversioned UND symbols")
PY
rm -f "$HERE/_und.txt"
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/libAequiStubs.so "$HERE/aequi_stub.c" -Wl,-soname,libAequiStubs.so
limactl copy "$VM":/tmp/libAequiStubs.so "$HERE/libAequiStubs.so"

echo "== trim heavy NEEDED, add stub, neuter ctors =="
cp -f "$SRC" "$HERE/libEp90_Aequi_trim.so"
PE_ARGS=(--set-soname libEp90_Aequi_trim.so)
for dep in $(limactl shell "$VM" -- readelf -d "$SRC" 2>/dev/null | awk -F'[][]' '/NEEDED/{print $2}'); do
  case "$dep" in
    libstdc++.so.6|libm.so.6|libgcc_s.so.1|libc.so.6) ;;
    *) PE_ARGS+=(--remove-needed "$dep") ;;
  esac
done
PE_ARGS+=(--add-needed libAequiStubs.so)
patchelf "${PE_ARGS[@]}" "$HERE/libEp90_Aequi_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libEp90_Aequi_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Aequi_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_aequi.c" \
  -L"$HERE" -lEp90_Aequi_trim -l:libAequiStubs.so \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_aequi

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_aequi; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Aequi_trim.so $CS/usr/lib/libEp90_Aequi_trim.so
  ln -sf $HERE/libAequiStubs.so $CS/usr/lib/libAequiStubs.so
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_aequi > /tmp/truth_aequi.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_aequi.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -30; exit 1
fi
