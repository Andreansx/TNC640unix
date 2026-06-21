#!/usr/bin/env bash
#
# build_and_verify_metaval.sh — decompile -> native ARM64 BEHAVIORAL
# reimplementation of the leaf C++ class methods of libtncMetaValue, proven
# OBSERVABLY EQUIVALENT to the proprietary i386 .so run under qemu-i386.
#
# These are genuine C++ member functions (mangled Itanium symbols, `this`):
#  - 5 static unit-conversion methods (InchPrecision, To{Non}Metric{Feed,Pos}Value)
#  - 6 CycMetaValue accessors (IsCardinal/IsInteger/IsReal/GetArraySize const;
#    IsSigned reads a double field; GetTextLength reads a uint field)
#  - 4 TncMetaValue pImpl accessors (IsSigned/GetTextLength/GetArraySize/IsSignedQ)
# The vtable-dispatching members (virtual through the pImpl) are excluded — see
# README/rebuilt.c.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libtncMetaValue.so"

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libtncMetaValue_arm64.dylib" "$HERE/libtncMetaValue_rebuilt.c"
clang -arch arm64 -O2 -I"$HERE" "$HERE/verify_metaval.c" "$HERE/libtncMetaValue_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libtncMetaValue_aarch64.so "$HERE/libtncMetaValue_rebuilt.c"
limactl copy "$VM":/tmp/libtncMetaValue_aarch64.so "$HERE/libtncMetaValue_aarch64.so"
file "$HERE/libtncMetaValue_arm64.dylib" "$HERE/libtncMetaValue_aarch64.so" | sed "s#$HERE/##"

echo "== generate stub for unversioned UND symbols (HeROS-runtime refs) =="
limactl shell "$VM" -- bash -c "i686-linux-gnu-nm -D --undefined-only '$SRC' 2>/dev/null" > "$HERE/_und.txt"
python3 - "$HERE/_und.txt" "$HERE/metaval_stub.c" <<'PY'
import sys
syms=[]
for ln in open(sys.argv[1]):
    p=ln.split()
    if not p: continue
    typ=p[0] if len(p)>1 else None
    name=p[-1]
    # keep only strong, UNVERSIONED undefined symbols (the HeROS-runtime refs);
    # glibc/libstdc++ refs are versioned (contain '@') and resolve from kept libs.
    if '@' in name: continue
    if typ not in ('U',): continue
    syms.append(name)
syms=sorted(set(syms))
with open(sys.argv[2],"w") as f:
    f.write("/* auto-generated: define every unversioned UND symbol so the\n"
            "   trimmed real .so satisfies its load-time data relocations. */\n")
    for s in syms:
        f.write(f"void {s}(void){{}}\n")
print(f"  stubbed {len(syms)} unversioned UND symbols")
PY
rm -f "$HERE/_und.txt"
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/libMetavalStubs.so "$HERE/metaval_stub.c" -Wl,-soname,libMetavalStubs.so
limactl copy "$VM":/tmp/libMetavalStubs.so "$HERE/libMetavalStubs.so"

echo "== make a loadable oracle: trim heavy NEEDED, add stub, neuter ctors =="
cp -f "$SRC" "$HERE/libtncMetaValue_trim.so"
PE_ARGS=(--set-soname libtncMetaValue_trim.so)
for dep in $(limactl shell "$VM" -- readelf -d "$SRC" 2>/dev/null | awk -F'[][]' '/NEEDED/{print $2}'); do
  case "$dep" in
    libstdc++.so.6|libm.so.6|libgcc_s.so.1|libc.so.6) ;;
    *) PE_ARGS+=(--remove-needed "$dep") ;;
  esac
done
PE_ARGS+=(--add-needed libMetavalStubs.so)
patchelf "${PE_ARGS[@]}" "$HERE/libtncMetaValue_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libtncMetaValue_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libtncMetaValue_trim.so" 2>/dev/null | grep NEEDED | sed 's/^/    /'

echo "== i386 ground-truth build against the trimmed REAL .so =="
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 -I"$HERE" "$HERE/verify_metaval.c" \
  -L"$HERE" -ltncMetaValue_trim -l:libMetavalStubs.so \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_metaval

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_metaval; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0; do ln -sf $X/$f $CS/lib/$f; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libtncMetaValue_trim.so $CS/usr/lib/libtncMetaValue_trim.so
  ln -sf $HERE/libMetavalStubs.so $CS/usr/lib/libMetavalStubs.so
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_metaval > /tmp/truth_metaval.txt
'

echo "== compare (exact ints, doubles within ~1 ULP) =="
limactl copy "$VM":/tmp/truth_metaval.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
python3 "$HERE/compare_metaval.py" "$HERE/truth.txt" "$HERE/recomp.txt"
