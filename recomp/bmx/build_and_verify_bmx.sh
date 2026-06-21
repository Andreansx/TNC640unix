#!/usr/bin/env bash
#
# build_and_verify_bmx.sh — decompile->ARM64 recompile of the BMX/BMP image-header
# leaf accessors of libplibpp, PROVEN byte-identical to the proprietary i386 .so.
#
# Verified subset (5 fns): bmxBmxInfo, bmxBmpInfo, bmxBmxVersion, bmxBmpData
# (single-field header reads), CheckSizeImage (24bpp padded-size computation with
# a write-back side effect).
#
# Oracle technique — MULTI-SONAME generalisation: this lib's surviving VERNEED
# entries come from several sonames (HEROSLIB_500.0/libheros.so.1,
# PNG16_0/libpng16.so.16, LIBJPEG_6.2/libjpeg.so.62). gen_oracle.py emits one stub
# .so per versioned soname (each carrying that soname + a version script) plus a
# general stub for the unversioned imports; we keep glibc + those sonames, trim the
# rest, and neuter the C++ static ctors.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
R="$REPO/work/target/rootfs"
BIN="$REPO/work/control/sysroot/heros5/bin"
VM=tnc
SRC="$BIN/libQsBmxImageLibraryNoDbidLookup.so"

echo "== native ARM64 build (macOS dylib + aarch64 Linux .so + harness) =="
clang -arch arm64 -O2 -shared -fPIC -o "$HERE/libplibpp_bmx_partial_arm64.dylib" "$HERE/libplibpp_bmx_partial_rebuilt.c"
clang -arch arm64 -O2 "$HERE/verify_bmx.c" "$HERE/libplibpp_bmx_partial_rebuilt.c" -o "$HERE/recomp_native"
"$HERE/recomp_native" > "$HERE/recomp.txt"
limactl shell "$VM" -- gcc -O2 -shared -fPIC -o /tmp/libplibpp_bmx_partial_aarch64.so "$HERE/libplibpp_bmx_partial_rebuilt.c"
limactl copy "$VM":/tmp/libplibpp_bmx_partial_aarch64.so "$HERE/libplibpp_bmx_partial_aarch64.so"
file "$HERE/libplibpp_bmx_partial_arm64.dylib" "$HERE/libplibpp_bmx_partial_aarch64.so" | sed "s#$HERE/##"

echo "== capture VERNEED version->soname map =="
limactl shell "$VM" -- readelf -V -W "$SRC" 2>/dev/null > "$HERE/_verneed.txt"
python3 - "$HERE/_verneed.txt" "$HERE/vermap.txt" <<'PY'
import sys, re
raw=open(sys.argv[1]).read()
out=[]; cur=None
for line in raw.splitlines():
    m=re.search(r'File:\s*(\S+)', line)
    if m: cur=m.group(1); continue
    n=re.search(r'Name:\s*(\S+)', line)
    if n and cur:
        v=n.group(1)
        if not any(s in v for s in ("GLIBC","GCC_","CXXABI")):
            out.append(f"{v} {cur}")
open(sys.argv[2],"w").write("\n".join(out)+("\n" if out else ""))
PY
rm -f "$HERE/_verneed.txt"
echo "  version->soname:"; sed 's/^/    /' "$HERE/vermap.txt"

echo "== generate multi-soname oracle stubs =="
limactl shell "$VM" -- readelf --dyn-syms -W "$SRC" 2>/dev/null > "$HERE/_dynsyms.txt"
python3 "$HERE/gen_oracle.py" "$HERE/_dynsyms.txt" "$HERE" "$HERE/vermap.txt"
rm -f "$HERE/_dynsyms.txt"

echo "== build each stub .so (i386, with its soname) =="
KEEP="libstdc++.so.6 libm.so.6 libgcc_s.so.1 libc.so.6 librt.so.1"
STUB_SONAMES=""
limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib -o /tmp/gen_stub.so "$HERE/gen_stub.c" -Wl,-soname,libGenStub.so
limactl copy "$VM":/tmp/gen_stub.so "$HERE/libGenStub.so"
while read -r kind a b c <&3; do
  [ "$kind" = "STUB" ] || continue
  soname="$a"; cfile="$b"; vfile="$c"; safe="${soname//./_}"
  limactl shell "$VM" -- i686-linux-gnu-gcc -shared -fPIC -O0 -nostdlib \
    -o "/tmp/stub_${safe}.so" "$HERE/$cfile" -Wl,--version-script="$HERE/$vfile" -Wl,-soname,"$soname" </dev/null
  limactl copy "$VM":"/tmp/stub_${safe}.so" "$HERE/stub_${safe}.so"
  ln -sf "stub_${safe}.so" "$HERE/$soname"
  KEEP="$KEEP $soname"; STUB_SONAMES="$STUB_SONAMES $soname"
done 3< "$HERE/oracle_manifest.txt"
echo "  keep sonames:$KEEP"

echo "== trim the REAL .so: remove every NEEDED not in the keep set, add gen stub =="
# IMPORTANT: do all NEEDED edits in ONE patchelf invocation — repeated patchelf
# calls on a larger .so accumulate layout damage (section-past-EOF), which the
# linker rejects.
cp -f "$SRC" "$HERE/libbmx_trim.so"
PE_ARGS=(--set-soname libbmx_trim.so)
for dep in $(limactl shell "$VM" -- readelf -d "$SRC" 2>/dev/null | awk -F'[][]' '/NEEDED/{print $2}'); do
  case " $KEEP " in *" $dep "*) ;; *) PE_ARGS+=(--remove-needed "$dep") ;; esac
done
PE_ARGS+=(--add-needed libGenStub.so)
patchelf "${PE_ARGS[@]}" "$HERE/libbmx_trim.so"
python3 "$HERE/neuter_init.py" "$HERE/libbmx_trim.so"
echo "  remaining NEEDED:"; limactl shell "$VM" -- readelf -d "$HERE/libbmx_trim.so" 2>/dev/null | grep NEEDED | sed "s/^/    /" || true

echo "== i386 ground-truth build against the trimmed REAL .so =="
# Link only against the trim; its kept NEEDED (incl. the versioned-soname stubs) is
# followed via -rpath-link so ld can resolve them, without explicitly linking each
# stub (several define the same Qt_5 version -> avoids multiple-definition).
limactl shell "$VM" -- i686-linux-gnu-gcc -O2 "$HERE/verify_bmx.c" \
  -L"$HERE" -lbmx_trim -l:libGenStub.so \
  -Wl,-rpath-link,"$HERE" -Wl,--unresolved-symbols=ignore-in-shared-libs -o /tmp/truth_bmx

echo "== run truth under qemu-i386 =="
limactl shell "$VM" -- bash -c '
  R="'"$R"'"; HERE="'"$HERE"'"; CS=/tmp/cs_bmx; X=/usr/i686-linux-gnu/lib
  rm -rf $CS; mkdir -p $CS/lib $CS/usr/lib
  for f in ld-linux.so.2 libc.so.6 libdl.so.2 libm.so.6 libgcc_s.so.1 libpthread.so.0 librt.so.1; do ln -sf $X/$f $CS/lib/$f 2>/dev/null; done
  ln -sf $R/usr/lib/libstdc++.so.6 $CS/usr/lib/libstdc++.so.6
  ln -sf $HERE/libbmx_trim.so $CS/usr/lib/libbmx_trim.so
  ln -sf $HERE/libGenStub.so $CS/usr/lib/libGenStub.so
  for s in '"$STUB_SONAMES"'; do ln -sf $HERE/$s $CS/usr/lib/$s; done
  qemu-i386 -L $CS -E LD_LIBRARY_PATH=/usr/lib:/lib /tmp/truth_bmx > /tmp/truth_bmx.txt
'

echo "== compare =="
limactl copy "$VM":/tmp/truth_bmx.txt "$HERE/truth.txt"
echo "  truth lines: $(wc -l < "$HERE/truth.txt")   recomp lines: $(wc -l < "$HERE/recomp.txt")"
if diff -q "$HERE/truth.txt" "$HERE/recomp.txt" >/dev/null; then
  echo "RESULT: IDENTICAL  native ARM64 == proprietary i386 on all cases"
  shasum -a 256 "$HERE/truth.txt" "$HERE/recomp.txt"
else
  echo "RESULT: MISMATCH"; diff "$HERE/truth.txt" "$HERE/recomp.txt" | head -40; exit 1
fi
