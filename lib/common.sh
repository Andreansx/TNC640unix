#!/usr/bin/env bash
# common.sh - shared helpers for the `tnc640` launcher. Sourced, not executed.
# shellcheck shell=bash

# --- repo paths ------------------------------------------------------------
# These are consumed by the launcher that sources this file (shellcheck can't see that).
# shellcheck disable=SC2034
{
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$REPO_ROOT/work"
VMDIR="$WORK/vm"            # host side of the shared folders (Install/IOsim/PLC/TNC)
VBOXDIR="$WORK/vbox"        # VirtualBox machine folder for our VM
RUNDIR="$WORK/run"          # screenshots / scratch
}

# --- defaults (override via config.env or environment) ---------------------
: "${VM:=TNC640}"
: "${CPUS:=2}"
: "${MEMORY:=3072}"         # MiB  (HEIDENHAIN requires >= 1536)
: "${VRAM:=64}"
: "${SSH_PORT:=2222}"       # host port forwarded to guest:22 (optional convenience)

# Load a user config if present (gitignored). Lets users pin VM name, package
# path, RAM, etc. without editing scripts.
if [ -f "$REPO_ROOT/config.env" ]; then
    # shellcheck disable=SC1091
    . "$REPO_ROOT/config.env"
fi

# --- pretty logging --------------------------------------------------------
if [ -t 1 ]; then
    _C_B=$'\033[1m'; _C_G=$'\033[32m'; _C_Y=$'\033[33m'; _C_R=$'\033[31m'; _C_0=$'\033[0m'
else
    _C_B=; _C_G=; _C_Y=; _C_R=; _C_0=
fi
log()  { printf '%s==>%s %s\n' "$_C_G$_C_B" "$_C_0" "$*"; }
info() { printf '    %s\n' "$*"; }
warn() { printf '%swarning:%s %s\n' "$_C_Y" "$_C_0" "$*" >&2; }
err()  { printf '%serror:%s %s\n' "$_C_R$_C_B" "$_C_0" "$*" >&2; }
die()  { err "$*"; exit 1; }

have() { command -v "$1" >/dev/null 2>&1; }

# --- VirtualBox ------------------------------------------------------------
VBOXMANAGE="${VBOXMANAGE:-}"
vbm() {
    if [ -z "$VBOXMANAGE" ]; then
        if have VBoxManage; then VBOXMANAGE=VBoxManage
        elif have vboxmanage; then VBOXMANAGE=vboxmanage
        else die "VBoxManage not found on PATH. Install VirtualBox 7.1 first (see ./tnc640 doctor)."; fi
    fi
    "$VBOXMANAGE" "$@"
}

require_vbox() {
    have VBoxManage || have vboxmanage || \
        die "VBoxManage not found. Install VirtualBox 7.1+ (https://www.virtualbox.org/wiki/Linux_Downloads)."
}

vm_exists()   { vbm showvminfo "$VM" >/dev/null 2>&1; }
vm_running()  { vbm list runningvms 2>/dev/null | grep -q "\"$VM\""; }

# --- locate the HEIDENHAIN package -----------------------------------------
# Echoes "OVA<TAB>SETUPZIP" for a given hint dir (or searches common spots).
# Returns non-zero if not found.
find_package() {
    local hint="${1:-}"
    local roots=()
    [ -n "$hint" ] && roots+=("$hint")
    [ -n "${PACKAGE:-}" ] && roots+=("$PACKAGE")
    roots+=("$REPO_ROOT/34059518" "$REPO_ROOT/34059518SP4" "$REPO_ROOT")
    local r ova zip
    for r in "${roots[@]}"; do
        [ -d "$r" ] || continue
        ova="$(find "$r" -type f -iname 'TNCvbProg.ova' 2>/dev/null | head -n1)"
        [ -n "$ova" ] || continue
        # setup.zip usually sits in a sibling prog/ folder of the package
        zip="$(find "$r" -type f -iname 'setup.zip' 2>/dev/null | head -n1)"
        printf '%s\t%s\n' "$ova" "$zip"
        return 0
    done
    return 1
}

# --- keypad python (one that can import PySide6) ---------------------------
KEYPAD_VENV="$REPO_ROOT/keypad/.venv"
keypad_python() {
    local cands=("$KEYPAD_VENV/bin/python" "$REPO_ROOT/.venv-keypad/bin/python" python3 python)
    local p
    for p in "${cands[@]}"; do
        if command -v "$p" >/dev/null 2>&1 && "$p" -c 'import PySide6' >/dev/null 2>&1; then
            printf '%s\n' "$p"; return 0
        fi
    done
    return 1
}

# Create keypad/.venv and install PySide6 into it.
install_keypad_deps() {
    have python3 || die "python3 not found (needed for the keypad)."
    log "creating keypad virtualenv at $KEYPAD_VENV"
    python3 -m venv "$KEYPAD_VENV" || die "could not create venv"
    "$KEYPAD_VENV/bin/pip" install --quiet --upgrade pip >/dev/null 2>&1 || true
    log "installing PySide6 (this can take a minute)"
    "$KEYPAD_VENV/bin/pip" install -r "$REPO_ROOT/keypad/requirements.txt" \
        || die "pip install failed"
    log "keypad dependencies installed"
}

mkdir -p "$RUNDIR" 2>/dev/null || true
