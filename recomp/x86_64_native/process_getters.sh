#!/bin/bash
# process_getters.sh <getterlist.txt>  (LIB\tSYM\tKIND\toffs)
# For each lib: split, gen rebuilt+harness, ship, verify; print result + a commit list.
set -u
LIST=$1
REPO=~/Documents/TNC640unix
F(){ grep -v "post-quantum\|store now\|may need\|openssh.com"; }
PASS=""
for lib in $(cut -f1 "$LIST" | sort -u); do
  n=$(grep -c "^$lib	" "$LIST")
  [ "$n" -eq 0 ] && continue
  short=$(echo "$lib" | sed 's/^libQs//' | tr 'A-Z' 'a-z')
  dir="qs_${short}"
  grep "^$lib	" "$LIST" | cut -f2- > /tmp/gl_$dir.txt
  python3 /tmp/gen_getters.py /tmp/gl_$dir.txt "$dir" >/dev/null
  mkdir -p "$REPO/recomp/$dir"
  cp /tmp/${dir}_rebuilt.c /tmp/verify_${dir}.c "$REPO/recomp/$dir/" 2>/dev/null
  echo "mkdir -p ~/tnc/recomp/$dir" | ssh -o ConnectTimeout=20 pawel wsl bash -s >/dev/null 2>&1
  cat "$REPO/recomp/$dir/${dir}_rebuilt.c" | ssh -o ConnectTimeout=20 pawel "wsl bash -c \"cat > ~/tnc/recomp/$dir/${dir}_rebuilt.c\"" 2>/dev/null
  cat "$REPO/recomp/$dir/verify_${dir}.c" | ssh -o ConnectTimeout=20 pawel "wsl bash -c \"cat > ~/tnc/recomp/$dir/verify_${dir}.c\"" 2>/dev/null
  res=$(echo "bash ~/tnc/nverify.sh $dir ${lib}.so" | ssh -o ConnectTimeout=20 pawel wsl bash -s 2>&1 | F)
  echo "$res  [$n getters]"
  if echo "$res" | grep -q IDENTICAL; then
    PASS="$PASS $dir:${lib}.so:$n"
    bash "$REPO/recomp/x86_64_native/build_arm64.sh" "$dir" "${lib}.so" >/dev/null 2>&1
  else
    rm -rf "$REPO/recomp/$dir"   # don't keep unverified dirs
  fi
done
echo "PASSLIST:$PASS"
