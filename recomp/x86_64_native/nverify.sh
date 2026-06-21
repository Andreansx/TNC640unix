#!/bin/bash
# nverify.sh <recomp_subdir> <oracle_so_basename> [rel_tol_exp]
set -u
DIR_NAME=$1; ORACLE=$2; RELEXP=${3:-12}
M32=$HOME/tnc/m32gcc; ORA=$HOME/tnc/oracles; REC=$HOME/tnc/recomp/$DIR_NAME
RUN=$HOME/tnc/lib32run; PE=$HOME/tnc/bin/patchelf
NEUTER=$HOME/tnc/recomp/aequi/neuter_init.py; FPDIFF=$HOME/tnc/fpdiff.py
W=$HOME/tnc/work/$DIR_NAME; mkdir -p "$W"; rm -f "$W"/*.so "$W"/*.ver "$W"/_s.s 2>/dev/null
STEM="${ORACLE%.so}"; TRIM="${STEM}_trim.so"
KEEP="libc.so.6 libm.so.6 libstdc++.so.6 libgcc_s.so.1 libdl.so.2 libpthread.so.0 librt.so.0 ld-linux.so.2"
# harness: prefer verify_*.c, else any *.c with main
H=$(ls "$REC"/verify_*.c 2>/dev/null | head -1)
[ -z "$H" ] && H=$(grep -l 'int main' "$REC"/*.c 2>/dev/null | head -1)
RB=$(ls "$REC"/*_rebuilt.c 2>/dev/null | head -1)
[ -z "$H" ] || [ -z "$RB" ] && { echo "$DIR_NAME: missing harness/rebuilt"; exit 2; }

cp -f "$ORA/$ORACLE" "$W/$TRIM"; $PE --set-soname "$TRIM" "$W/$TRIM"
# VERNEED: version -> soname (skip glibc-ish)
readelf -V "$W/$TRIM" 2>/dev/null | awk '
  /File:/ { for(i=1;i<=NF;i++) if($i=="File:") f=$(i+1) }
  /Name:.*Version:/ { for(i=1;i<=NF;i++) if($i=="Name:") v=$(i+1);
                      if (v !~ /GLIBC|CXXABI|GCC_/) print v, f }' | sort -u > "$W/verfile.txt"
VSONAMES=$(awk '{print $2}' "$W/verfile.txt" | sort -u)
# trim NEEDED (keep glibc-set + version-referenced sonames)
RM=""
for n in $(readelf -d "$W/$TRIM" | awk -F'[][]' '/NEEDED/{print $2}'); do
  keep=0; case " $KEEP " in *" $n "*) keep=1;; esac
  for s in $VSONAMES; do [ "$n" = "$s" ] && keep=1; done
  [ $keep -eq 0 ] && RM="$RM --remove-needed $n"
done
[ -n "$RM" ] && $PE $RM "$W/$TRIM"
python3 "$NEUTER" "$W/$TRIM" >/dev/null 2>&1

# versioned stubs (exact version match + dedup)
for soname in $VSONAMES; do
  vers=$(awk -v f="$soname" '$2==f{print $1}' "$W/verfile.txt")
  echo ".text" > "$W/_s.s"; : > "$W/_$soname.ver"; declare -A seen=()
  for v in $vers; do
    syms=$(readelf -W --dyn-syms "$W/$TRIM" | awk -v V="$v" '$7=="UND"{n=$8; i=index(n,"@"); if(i>0){b=substr(n,1,i-1); vr=substr(n,i+1); gsub(/^@+/,"",vr); if(vr==V) print b}}' | sort -u)
    printf '%s {\n  global:\n' "$v" >> "$W/_$soname.ver"
    for s in $syms; do
      printf '    %s;\n' "$s" >> "$W/_$soname.ver"
      [ -z "${seen[$s]:-}" ] && { printf '.globl %s\n%s:\n\tret\n' "$s" "$s" >> "$W/_s.s"; seen[$s]=1; }
    done
    printf '};\n' >> "$W/_$soname.ver"
  done
  unset seen
  $M32 -shared -nostdlib "$W/_s.s" -Wl,--version-script="$W/_$soname.ver" -Wl,-soname,"$soname" -o "$W/$soname" 2>>"$W/stub.err"
done
# unversioned proprietary UND -> weak ret stub
readelf -W --dyn-syms "$W/$TRIM" | awk '$7=="UND"{n=$8; if(n==""||n=="Name")next; if(index(n,"@")>0)next; print n}' | sort -u > "$W/stubsyms.txt"
{ echo ".text"; while read s; do printf '.weak %s\n%s:\n\tret\n' "$s" "$s"; done < "$W/stubsyms.txt"; } > "$W/wstub.s"
$M32 -shared -nostdlib -o "$W/stub.so" "$W/wstub.s"

$M32 -O2 "$H" -L"$W" -l:"$TRIM" -lm -ldl -Wl,-rpath-link,"$RUN" -Wl,--unresolved-symbols=ignore-in-shared-libs -o "$W/truth" 2>"$W/tb.err"
[ -x "$W/truth" ] || { echo "$DIR_NAME: TRUTH BUILD FAIL"; tail -4 "$W/tb.err"; exit 3; }
LD_PRELOAD="$W/stub.so" LD_LIBRARY_PATH="$W:$RUN:/lib/i386-linux-gnu" "$W/truth" > "$W/truth.txt" 2>"$W/truth.err"; TE=$?
gcc -O2 "$H" "$RB" -lm -o "$W/recomp" 2>"$W/rb.err"
[ -x "$W/recomp" ] || { echo "$DIR_NAME: RECOMP BUILD FAIL"; tail -4 "$W/rb.err"; exit 4; }
"$W/recomp" > "$W/recomp.txt" 2>/dev/null
TL=$(wc -l < "$W/truth.txt")
[ "$TE" -ne 0 ] || [ "$TL" -eq 0 ] && { echo "$DIR_NAME: TRUTH RUN FAIL exit=$TE lines=$TL"; head -2 "$W/truth.err"; exit 5; }
OUT=$(python3 "$FPDIFF" "$W/truth.txt" "$W/recomp.txt" "$RELEXP"); RC=$?
if [ $RC -eq 0 ]; then
  if echo "$OUT" | grep -q 'within_tol=0 fail=0'; then
    echo "$DIR_NAME [$ORACLE]: IDENTICAL ($TL lines) sha=$(sha256sum "$W/truth.txt"|cut -c1-12)"
  else
    echo "$DIR_NAME [$ORACLE]: EQUIVALENT ($TL lines) | $(echo "$OUT"|head -1|sed 's/^ *//')"
  fi
else
  echo "$DIR_NAME [$ORACLE]: FAIL | $(echo "$OUT"|head -1|sed 's/^ *//')"; echo "$OUT"|sed -n '2,4p'
fi
