#!/bin/bash
# build_arm64.sh <recomp_dir> <oracle_soname.so> — macOS arm64 dylib + Linux aarch64 .so
set -u
REPO=~/Documents/TNC640unix; dir=$1; soname=$2; stem=${soname%.so}
rb=$(ls "$REPO/recomp/$dir"/*_rebuilt.c | head -1); rbb=$(basename "$rb")
clang -arch arm64 -O2 -shared -fPIC "$rb" -o "$REPO/recomp/$dir/${stem}_arm64.dylib"
cat > /tmp/_a64.sh <<EOS
R=\$HOME/tnc/tc/root; GI=\$R/usr/lib/gcc-cross/aarch64-linux-gnu/13/include
\$HOME/tnc/a64gcc -isystem "\$GI" -nostdlib -shared -fPIC -O2 -Wl,-E -Wl,-soname,$soname ~/tnc/recomp/$dir/$rbb -o ~/tnc/recomp/$dir/${stem}_aarch64.so 2>&1 | head -3
EOS
ssh -o ConnectTimeout=25 pawel wsl bash -s < /tmp/_a64.sh 2>&1 | grep -v "post-quantum\|store now\|may need\|openssh.com"
printf 'cd ~/tnc/recomp/%s && tar cf - %s_aarch64.so\n' "$dir" "$stem" > /tmp/_pull.sh
ssh -o ConnectTimeout=25 pawel wsl bash -s < /tmp/_pull.sh 2>/dev/null | tar xf - -C "$REPO/recomp/$dir/"
echo "dylib=$(nm -gU "$REPO/recomp/$dir/${stem}_arm64.dylib" 2>/dev/null | grep -c 'T _') aarch64=$(test -f "$REPO/recomp/$dir/${stem}_aarch64.so" && echo OK || echo MISSING)"
