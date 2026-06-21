#!/usr/bin/env bash
#
# build_and_verify_dmathe.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the dmathe_* 2D-geometry math leaves of libEp90_Dm,
# proven OBSERVABLY EQUIVALENT (exact ints/bools, doubles within a tight FP
# tolerance) to the proprietary i386 .so run under qemu-i386. These are the
# "computed FP / libm" class excluded from the byte-identical bar.
#
# Oracle: the leaves call only libm (atan/sqrt/modf), but the .so drags the
# HeROS runtime + a HEROSLIB_500.0 VERNEED (sm_request/sm_release keyed to
# libheros.so.1). We trim the heavy NEEDED, satisfy the version requirement with
# a stub carrying soname libheros.so.1, stub the unversioned refs, neuter ctors.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libEp90_Dm.so"
RB=libEp90_Dm_dmathe_rebuilt.c

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Dm_dmathe_arm64.dylib" "$HERE/$RB"
clang -arch arm64 -O2 "$HERE/verify_dmathe.c" "$HERE/$RB" -lm -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Dm_dmathe_aarch64.so "$HERE/$RB"
limactl copy "$VM":/tmp/libEp90_Dm_dmathe_aarch64.so "$HERE/libEp90_Dm_dmathe_aarch64.so"
file "$HERE/libEp90_Dm_dmathe_arm64.dylib" "$HERE/libEp90_Dm_dmathe_aarch64.so" | sed "s#$HERE/##"

echo "== generate stub: unversioned UND refs + sm_request/sm_release@HEROSLIB_500.0 =="
limactl shell "$VM" -- bash -c "i686-linux-gnu-nm -D --undefined-only '$SRC' 2>/dev/null" > "$HERE/_und.txt"
python3 - "$HERE/_und.txt" "$HERE/dmathe_stub.c" <<'PY'
import sys
syms=[]
for ln in open(sys.argv[1]):
    p=ln.split()
    if len(p)<2: continue
    typ, name = p[0], p[-1]
    if '@' in name: continue          # versioned (glibc / HEROSLIB) handled separately
    if typ != 'U': continue
    syms.append(name)
syms=sorted(set(syms))
with open(sys.argv[2],"w") as f:
    f.write("/* auto-generated stub: unversioned UND refs + HEROSLIB version anchors */\n")
    for s in syms: f.write(f"void {s}(void){{}}\n")
    f.write("void sm_request(void){}\nvoid sm_release(void){}\n")
print(f"  stubbed {len(syms)} unversioned UND symbols + 2 HEROSLIB anchors")
PY
rm -f "$HERE/_und.txt"
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/libheros_stub.so \
  "$HERE/dmathe_stub.c" -Wl,--version-script="$HERE/dmathe_stub.ver" -Wl,-soname,libheros.so.1
limactl copy "$VM":/tmp/libheros_stub.so "$HERE/libheros_stub.so"
ln -sf libheros_stub.so "$HERE/libheros.so.1"

echo "== make a loadable oracle: trim heavy NEEDED (keep libheros.so.1 -> stub), neuter ctors =="
cp -f "$SRC" "$HERE/libEp90_Dm_trim.so"
PE_ARGS=(--set-soname libEp90_Dm_trim.so)
for dep in $(limactl shell "$VM" -- readelf -d "$SRC" 2>/dev/null | awk -F'[][]' '/NEEDED/{print $2}'); do
  case "$dep" in
    libheros.so.1|libstdc++.so.6|libm.so.6|libgcc_s.so.1|libc.so.6) ;;
    *) PE_ARGS+=(--remove-needed "$dep") ;;
  esac
done
patchelf "${PE_ARGS[@]}" "$HERE/libEp90_Dm_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libEp90_Dm_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libEp90_Dm_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_dmathe.c" \
  -L"$HERE" -lEp90_Dm_trim -l:libheros.so.1 -lm \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_dmathe

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_dmathe; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Dm_trim.so $CS/usr/lib/libEp90_Dm_trim.so
  ln -sf $HERE/libheros_stub.so $CS/usr/lib/libheros.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_dmathe > /tmp/truth_dmathe.txt
'

echo "== compare (exact ints/bools, doubles within FP tolerance) =="
limactl copy "$VM":/tmp/truth_dmathe.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
python3 "$HERE/compare_dmathe.py" "$HERE/truth.txt" "$HERE/recomp.txt"
