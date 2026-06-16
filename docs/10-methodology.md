# 10 — Methodology & Reproduction

How the analysis was done, so it can be re‑checked or extended. All steps were **read‑only**
against a copy of the package; during this static analysis the VM was never booted or modified, and nothing proprietary was
committed. (The dynamic bring-up that does boot it is covered in
[11-running-on-linux.md](11-running-on-linux.md).)

## Host & tools

- Static-analysis host: macOS (Apple Silicon); tools via Homebrew. The later dynamic bring-up
  was done on an x86-64 **Linux** host (see [11-running-on-linux.md](11-running-on-linux.md)),
  using the Linux equivalents (`qemu-nbd`/`losetup` + `mount` in place of `hdiutil`).
  - `sevenzip` (`7z`/`7zz`), `cabextract`, `binwalk` — archive/PE inspection
  - `qemu` (`qemu-img`) — VMDK → raw conversion
  - `e2fsprogs` (`debugfs`, `dumpe2fs`) — **read ext4 without mounting** (no root, no FUSE)
  - `msitools` (`msiinfo`, `msiextract`) — read Windows MSI tables & payload
  - built‑ins: `tar`/`bsdtar`, `unzip`, `strings`, `python3`, `hdiutil`

## Extraction map (where things came from)

```
work/
├─ ova/        TNCvbProg.ovf + TNCvbProg-disk001.vmdk (from the OVA via tar)
│              disk.raw  (qemu-img convert -O raw → sparse ~1 GB actual)
├─ extpack/    full JHIO extension pack (tar -xz)
├─ msi_prog/   payload + setup.ini from TNCvbProg.msi (msiextract)
├─ msi_base/   payload from TNCvbBase.msi, incl. control/*.exe|*.dll (msiextract)
└─ setupmeta/  SetupHeader.txt + *_vendorfileliste.txt from setup.zip (unzip, selective)
```

## Key techniques

### Reading the guest filesystem without mounting (ext4 on macOS)

macOS can't mount ext4, and `debugfs` wants a whole‑filesystem device. Trick:

```sh
# 1) Convert the stream-optimized VMDK to a sparse raw image
qemu-img convert -p -O raw TNCvbProg-disk001.vmdk disk.raw      # stays sparse on APFS

# 2) Attach read-only WITHOUT mounting → get per-partition device nodes
hdiutil attach -nomount -readonly -imagekey diskimage-class=CRawDiskImage disk.raw
#   → /dev/disk4, /dev/disk4s1 (HEROS5 /), s2 (BOOT), s5 (SYS), s6 (PLC), s7 (TNC)

# 3) Inspect each ext partition read-only with debugfs (no sudo needed; user owns the nodes)
DBG=/opt/homebrew/opt/e2fsprogs/sbin/debugfs
$DBG -R "ls -l /etc/init.d" /dev/disk4s1
$DBG -R "cat /etc/init.d/virtualbox" /dev/disk4s1
$DBG -R "show_super_stats -h" /dev/disk4s5     # volume label, size, etc.
# batch many reads: put commands in a file and use:  $DBG -f cmds.txt /dev/disk4s1

# detach when done
hdiutil detach /dev/disk4
```

### Reading MSI install logic

```sh
msiinfo tables  TNCvbProg.msi                      # list tables
msiinfo export  TNCvbProg.msi Property             # JhVmAppliance, JhSharedFolders, ...
msiinfo export  TNCvbProg.msi CustomAction         # TNCvbCreateVmS, ExtPackInstall, ...
msiextract -C out TNCvbBase.msi                    # extract payload (control/*.exe, etc.)
```

### Static binary inspection

`strings -n N <file>` then `egrep` for integration tokens (`/HEIDENHAIN/`, `IOsim`,
`vboxsf`, `19035`, `heuinput`, `GUI/Fullscreen`, `_JHIOIntern*`, `JHIOGet*`, COM CLSIDs,
VBOX_E_* errors). UTF‑16 (Windows) strings were pulled with a small Python regex when macOS
`strings` lacked the wide‑char flag. PE type identified with `file`.

### OVF / partition facts

`tar -xOf TNCvbProg.ova TNCvbProg.ovf` for the VM definition; a short Python MBR/EBR parser on
`disk.raw` for the partition table; `debugfs show_super_stats` for ext labels/sizes.

## Sub‑agent

The four official PDFs were digested by a separate research agent (read via the PDF reader),
producing the licensing/networking/demo facts used in [07](07-networking-licensing.md). One
nuance it inferred (screen = RDP/3389) was corrected against the binary evidence: the **local**
display is the native VirtualBox window; VRDE/3389 is for remote viewing.

## Pre‑push safety check (no proprietary blobs)

Before committing/pushing, verify nothing proprietary is staged:

```sh
# Should list ONLY docs/markdown/.gitignore — never *.ova/*.vmdk/*.dll/*.exe/*.pdf/*.msi/*.rpm
git status --porcelain
git ls-files | grep -Ei '\.(ova|vmdk|raw|img|qcow2|vdi|dll|exe|msi|cab|rpm|tar\.xz|vbox-extpack|pdf)$' \
  && echo "STOP: proprietary artifact staged!" || echo "clean: docs only"
```

## What was NOT done (future work)

- The VM was not booted; dynamic behaviour (actual guest properties at runtime, the keypad
  wire protocol, the handwheel‑19035 packet format, the live `IOsim` block layout) is **not**
  captured yet — that needs a running instance (ideally the Phase‑0 golden capture in
  [08](08-porting-unix-macos.md)).
- `target.tar.xz` was inventoried by file list only (77,306 files), not fully extracted; the
  NC core binaries (NCK/PLC/MMI, the handwheel server, the keypad input forwarder) were not
  individually analysed.
