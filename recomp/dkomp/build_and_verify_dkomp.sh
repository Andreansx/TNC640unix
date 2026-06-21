#!/usr/bin/env bash
#
# build_and_verify_dkomp.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the lock-free "huelle" doubly-linked-list navigators of
# libEp90_Dm (dkomp_nw_get_huelle_*), proven OBSERVABLY EQUIVALENT to the
# proprietary i386 .so under qemu-i386. These are MULTI-LEVEL POINTER CHASERS
# (handle->slot->container->node->next/prev, mutating cursor) — the class the
# byte-identical bar excludes (32-bit stored ptr can't address a 64-bit buffer).
# The harness builds an equivalent native list per-arch and compares the
# traversed node IDENTITIES (payload tags) + cursor state; output is
# deterministic so the proof is a plain diff.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libEp90_Dm.so"
RB=libEp90_Dm_dkomp_rebuilt.c

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libEp90_Dm_dkomp_arm64.dylib" "$HERE/$RB"
clang -arch arm64 -O2 -I"$HERE" "$HERE/verify_dkomp.c" "$HERE/$RB" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libEp90_Dm_dkomp_aarch64.so "$HERE/$RB"
limactl copy "$VM":/tmp/libEp90_Dm_dkomp_aarch64.so "$HERE/libEp90_Dm_dkomp_aarch64.so"
file "$HERE/libEp90_Dm_dkomp_arm64.dylib" "$HERE/libEp90_Dm_dkomp_aarch64.so" | sed "s#$HERE/##"

echo "== generate stub: unversioned UND refs + sm_request/sm_release@HEROSLIB_500.0 =="
limactl shell "$VM" -- bash -c "i686-linux-gnu-nm -D --undefined-only '$SRC' 2>/dev/null" > "$HERE/_und.txt"
python3 - "$HERE/_und.txt" "$HERE/dkomp_stub.c" <<'PY'
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
    f.write("/* auto-generated stub: unversioned UND refs + HEROSLIB version anchors */\n")
    for s in syms: f.write(f"void {s}(void){{}}\n")
    f.write("void sm_request(void){}\nvoid sm_release(void){}\n")
print(f"  stubbed {len(syms)} unversioned UND symbols + 2 HEROSLIB anchors")
PY
rm -f "$HERE/_und.txt"
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/libheros_stub.so \
  "$HERE/dkomp_stub.c" -Wl,--version-script="$HERE/dkomp_stub.ver" -Wl,-soname,libheros.so.1
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
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 -I"$HERE" "$HERE/verify_dkomp.c" \
  -L"$HERE" -lEp90_Dm_trim -l:libheros.so.1 \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_dkomp

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_dkomp; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libEp90_Dm_trim.so $CS/usr/lib/libEp90_Dm_trim.so
  ln -sf $HERE/libheros_stub.so $CS/usr/lib/libheros.so.1
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_dkomp > /tmp/truth_dkomp.txt
'

echo "== compare (deterministic node-tag traversal -> exact diff) =="
limactl copy "$VM":/tmp/truth_dkomp.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
