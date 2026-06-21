#!/usr/bin/env bash
#
# build_and_verify_fraescyc.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the coordinate-type classifier leaves of libEp90_Fraescyc
# (IsPolareLaenge, IsCartInkrement, IsPolarerWinkel — flat geotec flag reads),
# proven OBSERVABLY EQUIVALENT to the proprietary i386 .so under qemu-i386.
# No proprietary VERNEED -> metaval-style trim + auto-stub + neuter oracle.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libEp90_Fraescyc.so"
RB=libEp90_Fraescyc_rebuilt.c

echo "== native ARM64 build =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Fraescyc_arm64.dylib" "$HERE/$RB"
clang -arch arm64 -O2 "$HERE/verify_fraescyc.c" "$HERE/$RB" -lm -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Fraescyc_aarch64.so "$HERE/$RB"
limactl copy "$VM":/tmp/libEp90_Fraescyc_aarch64.so "$HERE/libEp90_Fraescyc_aarch64.so"
file "$HERE/libEp90_Fraescyc_arm64.dylib" "$HERE/libEp90_Fraescyc_aarch64.so" | sed "s#$HERE/##"

echo "== generate stub for unversioned UND symbols =="
limactl shell "$VM" -- bash -c "i686-linux-gnu-nm -D --undefined-only '$SRC' 2>/dev/null" > "$HERE/_und.txt"
python3 - "$HERE/_und.txt" "$HERE/fraescyc_stub.c" <<'PY'
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
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/libFraescycStubs.so "$HERE/fraescyc_stub.c" -Wl,-soname,libFraescycStubs.so
limactl copy "$VM":/tmp/libFraescycStubs.so "$HERE/libFraescycStubs.so"

echo "== trim heavy NEEDED, add stub, neuter ctors =="
cp -f "$SRC" "$HERE/libEp90_Fraescyc_trim.so"
PE_ARGS=(--set-soname libEp90_Fraescyc_trim.so)
for dep in $(limactl shell "$VM" -- readelf -d "$SRC" 2>/dev/null | awk -F'[][]' '/NEEDED/{print $2}'); do
  case "$dep" in
    libstdc++.so.6|libm.so.6|libgcc_s.so.1|libc.so.6) ;;
    *) PE_ARGS+=(--remove-needed "$dep") ;;
  esac
done
PE_ARGS+=(--add-needed libFraescycStubs.so)
patchelf "${PE_ARGS[@]}" "$HERE/libEp90_Fraescyc_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libEp90_Fraescyc_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Fraescyc_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_fraescyc.c" \
  -L"$HERE" -lEp90_Fraescyc_trim -l:libFraescycStubs.so -lm \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_fraescyc

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_fraescyc; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Fraescyc_trim.so $CS/usr/lib/libEp90_Fraescyc_trim.so
  ln -sf $HERE/libFraescycStubs.so $CS/usr/lib/libFraescycStubs.so
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_fraescyc > /tmp/truth_fraescyc.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_fraescyc.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
python3 "$HERE/compare_fraescyc.py" "$HERE/truth.txt" "$HERE/recomp.txt"
