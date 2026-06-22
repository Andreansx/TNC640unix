# TNC640unix — project tracker

Goal: run HEIDENHAIN's **TNC640 programming station** (PGM-Platz Virtual, all-i386 control)
on **Linux** (done) and **Apple-Silicon ARM64** (in progress). Background + measured findings:
`docs/` (start with `02-architecture.md`, `15-apple-silicon.md`, `16-arm64-decompilation-and-translation.md`).

Working environment (Apple Silicon M2 Max):
- ARM64 Linux VM: lima instance **`tnc`** (Ubuntu 26.04, vz). `limactl shell tnc -- <cmd>`.
- Host tools: Ghidra 12.1.2 + openjdk@21 (headless decompile), rizin, patchelf, lima.
- VM tools: qemu-user (`qemu-i386`), `gcc-i686-linux-gnu` cross-compiler, native `gcc`.
- Control extracted to `work/control/sysroot/` (binaries) + `work/target/rootfs/` (HeROS OS).
  Combined i386 sysroot for running: `work/target/rootfs` with `/heros5` grafted in.
- Decompiler pipeline: `work/re/scripts/DecompileToFile.java` + `batch_decompile.sh`.

> **NOTE — this tracker was consolidated for migration to an x86_64 host** (2026-06-21). The two
> auto-memory files that previously held the background below do NOT travel with the git clone, so
> their full content is inlined here (sections "Product architecture & background" and
> "Migration notes: moving the RE work to x86_64"). Everything needed to resume is in this file +
> `docs/` + `work/` + `recomp/`.

---

## Product architecture & background (HEIDENHAIN TNC640 PGM-Platz Virtual)

**Identity:** HEIDENHAIN **TNC640 Programming Station** (PGM-Platz Virtual), NC ident **340595**,
version **18 SP4**, extracted from the official download `34059518SP4/`. The shipped product is a
**Windows-native** package; goal of this repo is to run it on UNIX/macOS (and now ARM64). The user
already booted the bare `.vmdk` in a Linux VM but lacked the on-screen "steering panel".

**Architecture (as shipped on Windows):**
- Hypervisor: bundled **VirtualBox 7.1.4** (Win) runs the **HeROS5** guest (HEIDENHAIN Realtime OS,
  Yocto-based **x86_64** Linux, v5.18.04.002). VMware (VIX) is an alternative the installer accepts.
- Base VM = `base/TNCvbProg.ova` (OVF + 49 GB streamOptimized vmdk; partitions: **HEROS5** root,
  **BOOT**, **SYS**, **PLC**, **TNC**). On the base image SYS/PLC/TNC are EMPTY — the actual NC
  software (NCK/PLC/MMI) is flashed from `prog/setup.zip` (`target.tar.xz` 657 MB + SYS/PLC/TNC
  zips + RPMs) into those partitions on first install via the `Install` shared folder + HeROS
  `jhupdate`.
- **Host↔guest bridge** = (a) VirtualBox **shared folders** `Install`, `IOsim`, `PLC`, `TNC` mapped
  under the per-VM host folder; (b) VirtualBox **guest properties** `/HEIDENHAIN/*` (VMUSER/PW,
  CMD/Cmd, LC_ALL, CFG/Display/*); guest detects VBox via PCI `80eecafe` and loads
  vboxguest/vboxvideo/vboxsf in `/etc/init.d/virtualbox`.
- **Host control suite** (Qt6, in base MSI under `HEIDENHAIN\TNCvbBase\control\`):
  - `tncvbcntl.exe` (= `JHCNTLEXE`, the launcher: imports/creates VM, starts it as a **fullscreen
    native VirtualBox VM window** via `GUI/*` extradata, spawns the others),
  - **`keypad.exe`** (on-screen TNC keyboard / soft-key panel = the missing "steering panel"; feeds
    guest `heuinput` synthetic-input daemon via FIFO `/tmp/__heuinput`),
  - `handwheel.exe` (jog wheel, **QTcpSocket to guest port 19035**),
  - `jhiosimhostd.exe` + `iosim.dll` (= JHIOsim) + `plcmap.dll` (machine **PLC I/O simulation**).
- **JHIO extpack** `Heidenhain_VBoxJHIO_Extension_Pack-4.3.0-r6.vbox-extpack` = VBox HGCM host
  service `VBoxJHIO` (Win-only DLLs); bridges guest PLC I/O to host `iosim.dll` via a memory-mapped
  file in the `IOsim` shared folder, synced per PLC scan cycle. **Only host piece with no
  cross-platform binary.**
- Licensing: **SIK** options (`hegetsikopt`/`helicenseviewer`), USB dongles **MARX CrypToken**
  (VID 0d7a) + **AKS Hardlock** (VID 0529), and TE 5xx/6xx/7xx keyboard units (VID 1091) — all
  USB-passthrough device filters in the OVF. Without license → demo mode.

**Original porting blockers (Windows→UNIX):** x86_64 guest on Apple Silicon needs slow QEMU/UTM
emulation (VBox-ARM can't run x86 guests); the Windows Qt control suite + Win-only JHIO extpack must
be replaced/reimplemented or run via a Linux x86 VBox host. The reimplementation surface is: the Qt
apps + small iosim/plcmap DLLs + the documented **port 19035** / **heuinput FIFO** / shared-folder +
guestproperty protocol. (This is the "option A" path; the current focus below is "option B" =
translate/run the i386 control directly.)

**Workspace / extraction provenance:**
- Extracted artifacts live in `work/`: `ova/`, `extpack/`, `msi_prog/`, `msi_base/` (APPDIR:./control),
  `setupmeta/`. Raw disk = `work/ova/disk.raw` (sparse), inspected read-only via
  `hdiutil attach -nomount` (slices /dev/disk4s1..s7) + `debugfs` (brew e2fsprogs) — no mounting.
- **Control binaries are NOT in `target.tar.xz`** (that's just the HeROS OS). They live in
  `prog/setup.zip` → `TNC640_SYS.{1,2,3}.zip` → tree rooted at `heros5/bin/`. Extracted to
  `work/control/sysroot/`; HeROS OS to `work/target/rootfs/`.
- Host tools installed during this work: sevenzip, cabextract, binwalk, qemu, e2fsprogs, msitools,
  Ghidra 12.1.2 + openjdk@21, rizin 0.8.2, patchelf, lima 2.1.3.

---

## Inventory: 335 i386 ELF objects (87 executables + 248 shared libraries) — ALL Intel 80386

## Decompiled (Ghidra pseudo-C in `work/re/out/*.decomp.c`)

| Binary | Kind | Purpose | Notes |
|---|---|---|---|
| `libhdhinput.so` | lib | numeric input-field parse/validate | **recompiled+verified** ✓ |
| `liblsv2.so` | lib | LSV2 host comms protocol | ~29k lines; interop-relevant |
| `libProductId.so` | lib | product / SIK identity | |
| `libStartUpCtrl.so` | lib | control startup sequencing | |
| `libQsStartupController.so` | lib | Qt startup controller | |
| `libspi.so` | lib | serial peripheral interface | |
| `libEp90_Dintabs.so` | lib | DIN/ANSI thread tables (nominal Ø, pitch, undercut, tolerance) | **recompiled+verified** ✓ |
| `libplcbin.so` | lib | PLC-binary-module (.bin) file parser | **recompiled+verified** ✓ |
| `libEp90_Bohrcyc.so` | lib | drilling-cycle geometry | partial leaf (FP + external geom deps) |
| `libEp90_Errplib.so` | lib | EP90 error-class codes + facility-ID table | **recompiled+verified** ✓ |
| `libEp90_Wznorm.so` | lib | EP90 tool-type codec + tool-class classifiers | **recompiled+verified** ✓ |
| `libplccond.so` | lib | PLC condition evaluator (ASCII/stack helpers) | **recompiled+verified** ✓ |
| `libEp90_Gtlib.so` | lib | EP90 geometry/Geotec feature classifiers | **recompiled+verified** ✓ |
| `libEp90_Dm.so` | lib | geometry data-module (lists/FP) | scanned — clean yield low (FP + multi-level pointer chase) |
| `libtncMetaValue.so` | lib | meta-value typing | scanned — C++ class methods, not C leaves |
| `libplcmap.so` | lib | PLC I/O symbol map | **recompiled+verified** ✓ (Swap_d/_w, UQuadCompare, NumberOfCharacters) |
| `libfile.so` | lib | HeROS file layer | **recompiled+verified** ✓ (BitFieldTst, IsNcFile/IsAscFile, …) |
| `libplckernel.so` | lib | PLC kernel | decompiled; clean leaves reference globals (table extraction needed) |

## Recompiled to native ARM64 + verified equivalent (`recomp/`) — 73 batches, 490 functions
### (14 byte-identical libraries / 88 fns below; 13 behavioral-equivalence libraries / 112 fns in the next table; `gtlib2`: +13 fns, `geometri2`: +2 fns — see "x86_64 native migration" section at end)

| Binary | Artifacts | Verification |
|---|---|---|
| `libhdhinput.so` (13 fns) | `libhdhinput_arm64.dylib` (macOS), `libhdhinput_aarch64.so` (Linux) | byte-identical vs real i386 .so over 4000 vectors (same SHA-256); `recomp/build_and_verify.sh` |
| `libEp90_Dintabs.so` (7 fns) | `recomp/dintabs/libEp90_Dintabs_{arm64.dylib,aarch64.so}` | byte-identical over 7444-line sweep — full `GetNennd` index sweep, all 4 freistich scans × 1801 Ø, `NenndTblVgl` (same SHA-256); `recomp/dintabs/build_and_verify_dintabs.sh`. Tables lifted verbatim by `extract_tables.py`. |
| `libplcbin.so` (5 fns) | `recomp/plcbin/libplcbin_{arm64.dylib,aarch64.so}` | byte-identical on crafted `.bin` — version detect, BE field reads, both token-table mappings, SPLC derived fields, bincode streaming, all error codes (same SHA-256); `recomp/plcbin/build_and_verify_plcbin.sh`. Oracle = patchelf-trimmed real `.so` (heavy NEEDED removed). |
| `libEp90_Bohrcyc.so` (2 fns, **integer subset**) | `recomp/bohrcyc/libEp90_Bohrcyc_partial_{arm64.dylib,aarch64.so}` | byte-identical over 2258 vectors — `BCYC_Typisiere_Werkzeug` (full 32-bit range), `BCYC_Angetr_Werkz` (all 256 tool bytes, incl. exact `setbe`/`sete` upper-byte leakage); `recomp/bohrcyc/build_and_verify_bohrcyc.sh`. Partial: FP geom fns excluded (libm/double-rounding). |
| `libEp90_Errplib.so` (12 fns) | `recomp/errplib/libEp90_Errplib_partial_{arm64.dylib,aarch64.so}` | byte-identical 4232-line sweep — 9 `ERR_Is*` class predicates, `ERR_IsWarning`/`ERR_IsError` (`setbe` 32-bit leak), `ERRPLIB_GetFacilityID` (72-entry .rodata table lifted verbatim), `IsDPDemo`. `recomp/errplib/build_and_verify_errplib.sh`. |
| `libEp90_Wznorm.so` (5 fns) | `recomp/wznorm/libEp90_Wznorm_partial_{arm64.dylib,aarch64.so}` | byte-identical 11042-line sweep — `GeotecToIntWkzTyp`/`IntToGeotecWkzTyp`/`AsciiToGeotecWkzTyp` tool-type codec (signed div/mod, libc `strtol`), `WerkzeugTyp`/`WZ_IsAussenWkz` (struct +0xd8 decode + switch). `recomp/wznorm/build_and_verify_wznorm.sh`. |
| `libplccond.so` (8 fns) | `recomp/plccond/libplccond_partial_{arm64.dylib,aarch64.so}` | byte-identical 607-line sweep — `toupper/tolower_ASCII`, `IsPathSep`, `isNull`, and the fixed-capacity uint16 operand **stack** (`Push/Pop/Peek/IsStackEmpty`) over a caller flat buffer. `recomp/plccond/build_and_verify_plccond.sh`. |
| `libEp90_Gtlib.so` (9 fns) | `recomp/gtlib/libEp90_Gtlib_partial_{arm64.dylib,aarch64.so}` | byte-identical 127-line sweep — single-level `GTFIND_Is{Bohrung,FasRun,Freistich,Einstich,Gewinde}` (geotec tag @+0x54), `IsVariante`+`IsFigurRucksack` (both reproduce the i386 return-register leak). C++ mangled symbols bound via `__asm__` labels. `recomp/gtlib/build_and_verify_gtlib.sh`. |
| `libplcmap.so` (4 fns) | `recomp/plcmap/libplcmap_partial_{arm64.dylib,aarch64.so}` | byte-identical 5921-line sweep — `Swap_d`/`Swap_w` (endian), `UQuadCompare` (unsigned 64-bit compare), `NumberOfCharacters` (signed-decimal width, reproduces the i386 INT_MIN quirk). `recomp/plcmap/build_and_verify_plcmap.sh`. (`hexbyte`/`pmap_*` are leaves too but local symbols — not linkable.) |
| `libfile.so` (5 fns) | `recomp/file/libfile_partial_{arm64.dylib,aarch64.so}` | byte-identical 85-line sweep — `BitFieldTst` (signed bit-array test), `IsNcFile`/`IsAscFile` (file-type tag predicates), `FlServerListSize`, `read_mminch`. `recomp/file/build_and_verify_file.sh`. (`FlModAccess` is a leaf too but a local symbol.) |
| `libwinmgrlib.so` (6 fns) | `recomp/winmgr/libwinmgrlib_partial_{arm64.dylib,aarch64.so}` | byte-identical — `CheckWindow` (predicate + side-effect write), `WmGetMessageCount`, `WmMustConfirmEvent`, `AllocWindow` (counter bump), `WmGetLastError` (read-and-clear), `FreeWindow`. Single-level window-handle accessors; X11/bus deps trimmed. `recomp/winmgr/build_and_verify_winmgr.sh`. |
| `libConvertCfxNCK.so` (4 fns) | `recomp/cfxutil/libConvertCfxNCK_partial_{arm64.dylib,aarch64.so}` | byte-identical — `IsBinNumber`, `BinAtol` (binary string→int, 32-bit wrap), `IsUtf8` (BOM), `utf16_strlen`. Call-free string scanners shared across several control libs. `recomp/cfxutil/build_and_verify_cfxutil.sh`. |
| `libxmlreader.so` (3 fns) | `recomp/xmlhash/libxmlreader_partial_{arm64.dylib,aarch64.so}` | byte-identical — `XmlKeyHashBinary` (Jenkins one-at-a-time hash over signed bytes), `XmlHashSetKey`, `XmlHashSetValueAllocator`. `recomp/xmlhash/build_and_verify_xmlhash.sh`. |
| `libQsBmxImageLibraryNoDbidLookup.so` (5 fns) | `recomp/bmx/libplibpp_bmx_partial_{arm64.dylib,aarch64.so}` | byte-identical — `bmxBmxInfo/bmxBmpInfo/bmxBmxVersion/bmxBmpData` (image-header field reads), `CheckSizeImage` (24bpp padded-size calc + write-back). Needed the **multi-soname** oracle (Qt5 deps). `recomp/bmx/build_and_verify_bmx.sh`. |

## Recompiled to native ARM64 + BEHAVIORALLY verified (`recomp/`) — 13 libraries, 112 functions
The classes that the byte-identical bar EXCLUDES (computed FP/libm, C++ class methods with `this`,
pointer indirection) reimplemented natively and proven **observably equivalent**: identical
outputs for identical inputs, exact for ints/bools, doubles within a tight FP tolerance — measured
diff vs the genuine i386 `.so` under qemu-i386. (NOT same SHA-256; the `.text` genuinely differs.)

| Binary | Artifacts | Verification |
|---|---|---|
| `libEp90_Bohrcyc.so` (2 FP fns) | `recomp/bohrcyc_fp/libEp90_Bohrcyc_fp_{arm64.dylib,aarch64.so}` | 70957 vectors — `BCYC_EntnormiereWinkel` (angle de-norm ±2π), `BCYC_WinkelGleich` (sin/cos compare). Return codes exact, doubles **0 ULP**, 0 boundary flips. `recomp/bohrcyc_fp/build_and_verify_bohrcyc_fp.sh`. |
| `libtncMetaValue.so` (15 C++ methods) | `recomp/metaval/libtncMetaValue_{arm64.dylib,aarch64.so}` | 1283 vectors — 5 static unit-conv (To{Non}Metric{Feed,Pos}/InchPrecision, consts 2.54/25.4), 6 CycMetaValue + 4 TncMetaValue pImpl accessors. `this`-layout solved per-arch (mirror field order); bool methods read as `_Bool` (CONCAT31/setb leak). Ints exact, doubles 0 ULP. `recomp/metaval/build_and_verify_metaval.sh`. |
| `libProductId.so` (13 C++ methods) | `recomp/productid/libProductId_{arm64.dylib,aarch64.so}` | product-identity predicates driven over full control-mark range via `SetControlMarkForTest`; deterministic → **same SHA-256** on output. `recomp/productid/build_and_verify_productid.sh`. |
| `libEp90_Dm.so` (22 dmathe_* FP = COMPLETE family) | `recomp/dmathe/libEp90_Dm_dmathe_{arm64.dylib,aarch64.so}` | 12356 vectors — 2D geometry (NormWinkel/Wirein/VectorWinkel/Winkelstrecke/Distance/roundst/QuadGl/PunktDrehen/Turn180/CalcOeffWinkel/Perp/Tausche + bool wlinks/wrechts/InIntervall/antiparallel/SpGreater0/RadAufBogen/PktAufStrecke/KreisTangentenWinkel). atan/sqrt/modf; ints exact, doubles **0 ULP** (only sub-1e-16 cancellation residuals floored). `recomp/dmathe/build_and_verify_dmathe.sh`. |
| `libEp90_Dm.so` (23 dkomp_* ptr-chasers) | `recomp/dkomp/libEp90_Dm_dkomp_{arm64.dylib,aarch64.so}` | **MULTI-LEVEL POINTER CHASER** class — `dkomp_nw_get_{huelle,hilf}_*` doubly-linked-list navigators (handle→slot→container→node→next/prev + mutating cursor); 5 families: huelle (double-indirection) + hilf/rot3D/box3D (wrapper @+4/+0x18/+0x1c) + edge (caller-cursor descriptor). Per-arch-native list, compared by traversed node TAGS (not raw ptrs); deterministic → same SHA-256. `recomp/dkomp/build_and_verify_dkomp.sh`. |
| `libEp90_Geolib.so` (17 fns) | `recomp/geolib/libEp90_Geolib_{arm64.dylib,aarch64.so}` | 25124 vectors — geometry math: `abstand_pkt_pkt`/`abstand_pkt_gerade` (distances), `norm_winkel` (eps param), `compare_sinus_winkel`/`compare_winkel` (angle classifiers, arg order recovered from disasm), `oeffnungswinkel + GEOLIB_Is{Identisch,Invers,MathIdentisch,MathInvers} (flat geo-struct element predicates: same/reverse/collinear, line+arc). ints exact, doubles **0 ULP**. `recomp/geolib/build_and_verify_geolib.sh`. |
| `libEp90_Geometri.so` (3 fns) | `recomp/geometri/libEp90_Geometri_{arm64.dylib,aarch64.so}` | 720 vectors — coordinate-type classifiers `IsPolareLaenge`/`IsCartInkrement`/`IsPolarerWinkel` (flat geotec flag reads @0x58/0x5c gate + 0xd8/0xdc/0xf0/0xf4 by mask, C++ mangled). deterministic → same SHA-256. `recomp/geometri/build_and_verify_geometri.sh`. |
| `libEp90_Aequi.so` (3 fns) | `recomp/aequi/libEp90_Aequi_{arm64.dylib,aarch64.so}` | 115 vectors — `get_laengentoleranz`/`AEQ_GetLaengentoleranz` (tolerance accessors) + `anz_same_level` (singly-linked-list length via +4 link, per-arch node). C++ mangled; deterministic → same SHA-256. `recomp/aequi/build_and_verify_aequi.sh`. |
| `libEp90_Anfahr.so` (2 fns) | `recomp/anfahr/libEp90_Anfahr_{arm64.dylib,aarch64.so}` | 22669 vectors — `EckenWinkel` (corner angle ±2π fold) + `get_einfahr_radius` (entry-radius clamp, **disasm recovery** of a Ghidra-void function's st0 return). 0 ULP. `recomp/anfahr/build_and_verify_anfahr.sh`. |
| `libEp90_Gewcyc.so` (6 fns) | `recomp/gewcyc/libEp90_Gewcyc_{arm64.dylib,aarch64.so}` | 310 vectors — `GCYC_Geostart`/`GCYC_Geoziel` (geotec start/end point via a pointer-chased direction flag @*(g+0x14)+0x7b, per-arch geotec) + `GCYC_SimpelAbhebeWinkel` (lift-angle switch). 0 ULP. `recomp/gewcyc/build_and_verify_gewcyc.sh`. |
| `libEp90_Cyckkorr.so` (2 fns) | `recomp/cyckkorr/libEp90_Cyckkorr_{arm64.dylib,aarch64.so}` | 1340 vectors — `renormiere_punkt` (quadrant point rotation via bit-exact ~1e-16 sin/cos residuals, 2 flag variants) + `ckk_uebertrage_attribute` (flat geotec attribute-field copy). 0 ULP. `recomp/cyckkorr/build_and_verify_cyckkorr.sh`. |
| `libEp90_Fraescyc.so` (3 fns) | `recomp/fraescyc/libEp90_Fraescyc_{arm64.dylib,aarch64.so}` | 8064 vectors — `FCYC_FraesTiefe`/`FCYC_AbhebeLaenge`/`FCYC_VorschubArt` (flat tec_cycfraes_rt accessors). 0 ULP. (`FCYC_AnzahlSchichten` excluded: x87 fisttpl 80-bit truncation + AT&T-reversed operands not bit-reproducible.) `recomp/fraescyc/build_and_verify_fraescyc.sh`. |
| `libEp90_Drehcyc.so` (1 fn) | `recomp/drehcyc/libEp90_Drehcyc_{arm64.dylib,aarch64.so}` | 700 vectors — `is_aufmass_aktiv` (allowance-active predicate; `aufmass_rt` passed BY VALUE, offsets from disasm). same SHA-256. (Drehcyc is a fn-pointer-table arch — most exports are runtime forwarder thunks.) `recomp/drehcyc/build_and_verify_drehcyc.sh`. |

### Behavioral method (how it differs from byte-identical)
Verification standard relaxes from "same SHA-256" to "same observable outputs": exact for
integer/boolean returns, FP tolerance (ULP/relative + a near-zero absolute floor) for computed
doubles. Two key techniques: (1) **per-arch-native objects** — for `this`/pointer-chasing C++
methods, the harness mirrors the class FIELD ORDER and builds the object per-arch from identical
LOGICAL inputs (i386 reproduces 4-byte-ptr offsets, ARM uses 8-byte), so the same harness drives
both sides past the "32-bit stored ptr can't address 64-bit buffer" wall. (2) **bool low-byte
contract** — i386 `bool` returns only define `al`; the upper eax bytes (CONCAT31/setb leak,
load-address-dependent) are read off by declaring the harness prototype `_Bool`. Same oracle recipe
(trim NEEDED, soname/version stub, neuter ctors) as the byte-identical set.

## Method refinements (this session) — the oracle recipe generalised
For C++ libs whose leaf functions are libc-only but whose `.so` drags the HeROS runtime:
1. **trim** heavy `DT_NEEDED` (patchelf `--remove-needed`), keep libstdc++/libm/libgcc_s/libc;
2. **stub** the residual non-glibc imports with an auto-generated `.so` (symbols the leaves never
   touch) — when a `HEROSLIB_500.0`/`JHVOLUMELIB_500.0`/`Qt_5`/… VERNEED remains, give the stub that
   library's **soname** + a version script so the load-time version check passes. For libs whose
   surviving VERNEED spans **several** sonames (e.g. Qt: `Qt_5` from Svg/Gui/Core/Quick), `recomp/bmx/
   gen_oracle.py` emits one stub per file, each defining every version it's listed for;
3. **neuter** the C++ static ctors/dtors (`recomp/*/neuter_init.py` zeroes DT_INIT/FINI[_ARRAY]) —
   leaf functions need no global init, and the ctors would call into the trimmed-away runtime.
The recompiled `.text` of each verified function is the genuine proprietary machine code, unchanged.
Gotchas: do all patchelf NEEDED edits in ONE invocation (repeated calls corrupt larger `.so` →
"section past EOF", rejected by ld.bfd); a candidate must be EXPORTED in `.dynsym` to be the oracle.

## Candidate next decompile/recompile targets — MORE LEAVES REMAIN (set NOT exhausted)
- Still-unharvested: more `libEp90_Gtlib` single-field classifiers (IsGewinde-style, ~40 candidates),
  `libplckernel` integer accessors, `libProductId`/`libspi`/`libStartUpCtrl` (already decompiled),
  un-scanned libs (`libplcbin` siblings, `libEp90_Aeplib/Errplib/…`). NOTE: confirm a candidate is
  EXPORTED in `.dynsym` before building — local symbols (e.g. `hexbyte`, `FlModAccess`) aren't
  dynamically linkable, so they can't be the truth oracle even though their machine code is genuine.
- Excluded by the byte-identical bar: **C++ class methods** (libtncMetaValue, libProductId — vtables/`this`),
  **multi-level pointer chasers** (Gtlib/Dm list walkers — 32-bit stored pointers can't address a
  64-bit buffer), and **computed FP / libm** (Ep90 geometry, `dmathe_*` — x87-vs-SSE / double-rounding).
- Recompile generalises to PURE LEAF code only (no C++ classes/state, no FP boundary) — see doc 16 §3/§3a.

---

## TRANSLATION PORT ROADMAP (current focus — option B: run unmodified i386 control on native ARM64)

Status: i386 userspace translation **works** on the M2 (NCK interpolator loads its full 100-lib
closure and runs its own init). First hard blocker = the **HeROS kernel API**.

### Phase 1 — Understand the `heroscall` kernel ABI  ✓ DONE
- [x] 1.1 `222` = heros.ko custom gateway (unassigned in mainline i386); `407` = `clock_nanosleep_time64` (real, qemu-i386 lacks it — secondary)
- [x] 1.2 heroscall is issued via libc **`syscall()`** → **LD_PRELOAD emulation is viable, no qemu patch needed.** Probe: `work/re/shim/heroscall_probe.c`
- [x] 1.3 `heros.ko` `sym.heros_entry` is a **pSOS-style RTOS dispatcher**. ABI: `syscall(222, cmd, param_ptr, arg)`, `cmd = 0x1234_NNNN`. Full command map decompiled → `work/re/out/heros_ko.decomp.c`:
  `01 T_ident · 02 T_start · 09 T_name · 0a Q_create · 0d Q_send · 0e Q_read · 10 Ev_send · 11 Ev_receive · 15 Sm_create · 18 Sm_request · 27 Sys_getenv · …`
- [x] 1.4 Init's actual queries captured (the shim runs in-process, derefs the arg ptr):
  - `Sys_getenv` names: **SYS, SYS_NAME, USR, USR_NAME, OEM, OEM_NAME, OEME, OEME_NAME, EXECDIR, EXECDIRH, EXECBAT** (partition/identity/exec paths)
  - `T_ident` name=0 (ident self) → needs a valid task id; plus `Sm_create`/`Q_create`/`T_name` handle setup

### Phase 2 — Build the LD_PRELOAD heroscall emulator  ✓ DONE (passes blockers #1–#4)
Built natively on the x86_64 box (no qemu). Sources in **`emulator/`**, full write-up in
**`docs/17-heroscall-emulator.md`**. The NCK now boots through its whole RTOS/kernel-API init.
- [x] 2.1 Skeleton: interpose `syscall()`, dispatch `cmd & 0xff`, pass non-222 to raw `int 0x80`.
- [x] 2.2 `Sys_getenv` — values recovered VERBATIM from the control's own boot scripts
  (`heros5/bin/../application` + `appproduct`): `SYS=/mnt/sys OEM=/mnt/plc USR=/mnt/tnc
  OEME=/mnt/plce EXECDIRH=/mnt/sys/heros5/bin EXECBAT=/mnt/sys/batch/heros5 SYS_NAME=SYSTEM:
  OEM_NAME=PLC: OEME_NAME=PLCE: USR_NAME=TNC:`. Served via `getenv()` from `run_nck.sh`.
- [x] 2.3 `T_ident(self)`→nonzero tid; `Sm/Q/M_create`→fake handles; **`M_ident`→nonzero region id,
  `M_attach`→a real 64 MB zeroed `mmap`** (this is what clears PciHardware).
- [x] 2.4 Past `PciHardware::Exception` ✓. Then past `FProcess` argv assert (#3) with
  `-p=~/IPO IPO -k=NC -M` (argv recovered from `batch/TNC640heros.txt`), then past IPO option
  parsing → reached blocker **#5 = the configuration subsystem**.

### Phase 3 — Iterate the blocker chain to a running control  ← NEXT
Blocker #5 is the first **application-level**, inherently **multi-process** dependency:
`CfgMailslot::GetData` (libbackend-server.so) is a CLIENT of a config **server** over a HeROS
mailslot queue (`CfgMailslotQueue::CreateQueue`+`GetData`). IPO standalone has no server → the
"NC" channel-group lookup returns err 42 → IPO aborts (misleading "Invalid Command Option -k").
- [ ] 3.1 Upgrade the emulator's RTOS primitives from in-process fakes to **real cross-process IPC**
  (SysV shm/sem/msg keyed by the HeROS names) so forked peers share one namespace.
- [ ] 3.2 Run `AppStartMP.elf` (the process manager) so it spawns the constellation
  (IPO + PLC + config server + Geo + …) which then answer each other's config/queue requests.
- [ ] 3.3 Then: message bus (`libGMessage*`), FUSE backends, device nodes, X/Qt MMI. Full boot to
  the Qt MMI remains the documented infeasible/legally-barred ceiling.

### Known blockers (live)
- **#1 `/dev/herosapi` open** — PASSED (`emulator/herosapi_shim.c`).
- **#2 `heroscall` syscall 222 / `PciHardware::Exception`** — PASSED. `M_ident("IPO_SHARED_MEMORY")`
  + `M_attach` now serve a real zeroed region (`emulator/heroscall_emu.c`).
- **#3 `FProcess` argv assert** — PASSED (correct argv).  **#4 empty `Sys_getenv`** — PASSED (real env).
- **#5 config subsystem / IPO connect-ACK** — ★ **SOLVED 2026-06-22 (commits 92a98c5/6108aef): IPO
  CONNECTS.** ConfigServer's `SendConnected` can NEVER flush IPO — clients are inserted into the client
  Rb_tree only in `CfgServer::Initialize@0x187b4a`, never in `OnConnectClient` (which only `_Rb_tree::find`s),
  so IPO is never registered (the SIK/Hws-stub "run-up" story was a layer below this). The fix bypasses
  ConfigServer: **`HEROSCALL_INJECT_ACK`** synthesizes IPO's `CfgClientIsConnected`(id **0x170100**;
  fields clientId/id/success; **success=OK**; schema decoded from `.rodata 0x230b80/0x230bc0`) and posts it
  straight to IPO's reply queue. IPO reads it, prints **"Connected"**, and proceeds (`OnCfgClientIsConnected
  @0x1a72d0` → `CfgMailslotQueue::Create` → `SyncMessage` → `AskIpoConditions`). Also proven en route:
  synthetic **`UpdNewState`** (id 0x1f0320) deserializes + drives `OnUpdNewState` — the GMessage deserializer
  is **schema-driven**, so messages are built from the `.rodata` schema templates (gated `INJECT_UPD`).
  Run-up fixes retained (SIK/Hws stub, `Ev_receive`, `MAXQ`). See docs/17 §Update(2026-06-22).
- **#6 config-data round-trip (NEW frontier, past the connect)** — IPO reaches
  `IpoController/IpoKonfig::CheckOptions()` and fails `-k=NC` ("Invalid Command Option -k", AFTER "Connected").
  ROOT CAUSE FOUND: **ConfigServer's channel-group DB is empty** — it reads the config INDEX
  (`jhconfigfiles.cfg`, direct `-f=` path) but the listed files use **volume paths** (`SYS:\config\tnc.cfg`).
  Those resolve via the HeROS **volume manager** (`libjhvolume` → `/etc/jhvolume`), which was MISSING →
  the control spun retrying `open("/etc/jhvolume")=ENOENT` and never loaded `tnc.cfg` (which DOES define
  "NC"). `emulator/setup_jhvolume.sh` populates `/etc/jhvolume`. **Volume resolution FIXED**: register the
  names WITH the trailing colon (`jhvolume --set "SYS:" /tmp/s`, not `"SYS"`) — then `SYS:\config\tnc.cfg`
  resolves to `/tmp/s/config/tnc.cfg` (the colon form the control uses). STILL not sufficient: `strace`
  shows ConfigServer reads only the INDEX and **never opens `tnc.cfg`** even with resolution working, and
  fails on the runtime-generated productid cache (`/mnt/sys/cache/nckern/productid/*.conf`, ENOENT — uses
  a hardcoded `/mnt/sys`, not `$SYS`). So the remaining gap is the config-LOAD mechanism (productid gate /
  binary cache / a deferred "activate configuration" trigger the absent MMI/constellation sends), NOT the
  path layer. CONFIG-LOAD PATH FOUND: `ReadDataFiles@0x214540` (the file loader) ← `ReadConfigDataSet
  @0x229d50` ← `OnUpdNewState` (NOT `OnRereadData`, which is write-back/refresh). `HEROSCALL_INJECT_REREAD`
  posts a synthetic **UpdNewState** (id 0x1f0320) onto CfgServerQueue at run-up; verified ConfigServer
  reads it, runs `OnUpdNewState` (`Q_ident "Nc"`), and `ReadConfigDataSet` FIRES — broadcasting real config
  to QEvtServer (a 4380-byte payload + 664/608/550/539B…). So the load path EXECUTES. BUT `tnc.cfg` is
  still never opened and IPO still fails — `ReadDataFiles` runs yet skips the channel-group file. Remaining
  gate is INSIDE `ReadDataFiles`. Chain fully RE'd: jhconfigfiles.cfg IS read+parsed (strace: read=2736B
  `CfgJhConfigDataFiles(...jhDataFiles:=[...]`); `ReadConfigDataSet`→`ReadConfigDataDir@0x2150a0`→
  `SetupDirInfo@0x2a2a60` (registers via `CfgStore::DataFile`) + `ReadDataFiles`→loop `CntDataFiles`×
  `PrepareFile@0x20d9a0`(`FSystemPathname::IsAFile` exists-check→`ReadHeader`, else `MissingFile`). ★ ROOT
  CAUSE SURFACED: ConfigServer's stdout shows it expects the config at a HARDCODED **`/mnt/sys/config`** and
  **encfs-mounts an ENCRYPTED subdir** there: `encdir: Create directory failed ... /mnt/sys/config/jh_int` +
  `sh: encfs: not found` + `umount: /mnt/sys/config/jh_int`. So the config dir is an **encfs (encrypted
  filesystem) mount** the control sets up at startup — standalone it fails (encfs not installed; /mnt/sys→
  sysroot is READ-ONLY so encdir can't create jh_int; jh_int needs the OEM key). **IMPLEMENTED**
  `emulator/setup_config_env.sh` (install encfs 1.9.5 + writable `/mnt/sys/config` + colon-form volumes→
  `/mnt/sys`). RESULT: encfs is a RED HERRING — jh_int is OEM-secret storage and tnc.cfg is PLAINTEXT; the
  encdir mount still fails under qemu (FUSE/`unshare`) but non-fatally. ★ DECISIVE host-strace
  (`-e openat,newfstatat,statx,access`): ConfigServer NEVER opens OR STATS tnc.cfg or any data `.cfg/.atr`
  (0 touched). So `PrepareFile/IsAFile` is never reached ⇒ CfgStore per-layer registration is EMPTY
  (`CntDataFiles=0`) ⇒ `ReadDataFiles` skips every file. jhconfigfiles.cfg IS parsed (2736B) but
  `SetupDirInfo→CfgStore::DataFile` registers nothing for the layer; the 4380B config ConfigServer
  broadcasts comes from a CACHE (`/tmp/CBIOS_MAPPED_FILE_REV_200`), not the files. So the real gate is the
  per-layer data-file REGISTRATION. ★★ ABSOLUTE ROOT CAUSE (corrected — encfs is NOT a red herring): the
  config DATA dir IS an **encfs-encrypted mount**. ConfigServer reads config from `/mnt/sys/config/jh_int`
  (the encfs DECRYPTED view of the encrypted store `_jh_int`); strace shows it opens `jh_int`(O_DIRECTORY)
  + `jh_int/layout`, NEVER the plaintext `/mnt/sys/config/*.cfg`. `encDir` is a C++ class in libConfigSystem
  (encDir::start/stop/pathDecrypt) that at startup writes a FRESH `_jh_int/.encfs6.xml` (O_TRUNC) +
  `unshare(CLONE_NEWNS)` + encfs-mounts `jh_int`. TWO sub-gates: (1) **unshare needs root** — as my user it
  fails (`error unshare ret`/`error encfs`); ★ run ConfigServer as ROOT (sudo qemu-i386, `/dev/fuse`
  present) and the encDir errors VANISH, the mount succeeds (`encdir: mounted`). (2) **the encrypted store
  is EMPTY** — encDir makes a fresh encfs so `jh_int` is empty; my extraction has the PLAINTEXT config
  (tnc.cfg @ /mnt/sys/config) but NOT the encrypted `_jh_int` (built at install/flash time), and ConfigServer
  does NOT populate jh_int from plaintext → `jh_int` empty → `CntDataFiles=0` → tnc.cfg never read. The 4380B
  config it broadcasts is from a cache (`/tmp/CBIOS_MAPPED_FILE`), not the files. NEXT (the real install
  step): run ConfigServer as ROOT and make the encDir store contain the config — a config INSTALL that
  writes the plaintext config through ConfigServer into jh_int (CfgWriteData), or pre-encrypt it into
  `_jh_int` and stop the O_TRUNC re-init. FINAL: the store is **SIK-KEYED** — `encDir::start` ← 
  `ServerHelper::DecryptConfig@0x2a14b0`; crypto = `sik_encrypt`/`TEOS_DoEncryptRSA` (the SIK/license).
  `DecryptConfig` READS the already-encrypted config from jh_int (→ `CfgStore::HashObj`); it does NOT
  migrate plaintext. ★ CORRECTION (NOT license-barred): the encfs invocation is `echo
  "Yomxn8YJyvrbNli62Rpl" | encfs -S _jh_int jh_int` — the password is a FIXED, DETERMINISTIC string (not
  the dongle). encfs round-trips fine; the encryption is just data-at-rest with a known key. Clean test:
  ConfigServer creates an EMPTY encfs (0 files in _jh_int) and does NOT migrate the plaintext; the volume
  key is random per-create so pre-populating can't align. So the config must be written THROUGH ConfigServer
  via **`CfgWriteData`** (`CfgServer::OnWriteData@0x225510`) — the jhupdate/installer mechanism: it encrypts
  each entity into its current store, then serves it. NEXT (tractable, the real step): reimplement the
  config INSTALL — construct `CfgWriteData` for the config (minimally the "NC" channel group) and send it to
  ConfigServer running as ROOT (encDir's unshare needs CAP_SYS_ADMIN; /dev/fuse present). Substantial
  GMessage construction (like INJECT_ACK but the full config schema), but engineering — NOT a legal ceiling.
  ★ 2nd CORRECTION: the encfs is a DETOUR. Decisive test (jh_int = PLAIN DIR with the 27 config files +
  no-op encfs): ConfigServer ENUMERATES it (`getdents64` on jh_int + descends into `jh_int/layout`) yet
  opens 0 data .cfg and IPO still fails -k=NC. So config presence+enumeration is NOT sufficient — the gate
  is the per-layer data-file REGISTRATION (`SetupDirInfo@0x2a2a60`→`CfgStore::DataFile`; `CntDataFiles=0`),
  INDEPENDENT of the encfs. ConfigServer reads `jh_int/layout/` (subdir-structured), so the data files are
  likely expected in a per-LAYER subdir structure and/or registration is gated on the absent productid
  cache (controlmark selects the layer/variant). NEXT (the actual gate): RE `SetupDirInfo`/`ReadConfigDataDir`
  for the layer/dir-structure + productid it needs to register the jhDataFiles. This is the registration
  subsystem — not the encfs, not licensing. `emulator/setup_config_env.sh` holds the env. ★★ ULTIMATE GATE:
  the registration is gated on the **productid** (control mark). `libProductId` reads
  `/mnt/sys/cache/nckern/productid/*.conf`; ConfigServer does `ProductId::GetControlMark()` +
  **`OptionLib::GetOptionTable(CfgControlMark, SikGeneration)`** — control-mark + **SIK** select the
  option/config table driving the layer. The productid cache is written by **`AppStartMP.elf`**, which —
  tried standalone — **hangs at "waiting for X-Server startup"**: it needs the full GUI boot. So blocker #6
  ultimately requires the FULL BOOT (AppStartMP + X to generate the productid) and the SIK (the option
  table) — the documented infeasible/legally-barred ceiling (Qt MMI / X / constellation / license). The
  full-system qemu path works because the productid was generated at boot + the SIK from the dongle/demo at
  flash. This is the honest endpoint of Track B (userspace emulation). (Connect, blocker #5, solid.)
  ★ UPDATE — productid is SYNTHESIZABLE without the full boot: `ProductId::Update`→`ProductInfo::Init@0x1600`
  reads the confs with C++ **ifstream** (`operator>>(int&)` for controlmark.conf→+0x90, `_M_extract<bool>` for
  the bool confs +0x94/+0x95/+0x96) — i.e. PLAIN ASCII (an int or 0/1 per file). Wrote them (controlmark=0,
  progstationversion=1, virtualmachine=1, …): ConfigServer now READS all 5 (no more ENOENT). BUT registration
  STILL 0 / IPO still fails: necessary-not-sufficient. Remaining gate = the control-mark VALUE (0 yields a
  wrong/empty `GetOptionTable` → wrong layer) and/or the per-LAYER DIR LAYOUT (ConfigServer descends into
  `jh_int/layout/`, so flat tnc.cfg isn't where `ConfigDataFile`/`DataStore::RetrieveLayer(LayerNr)` looks).
  So the productid is a DONE step; NEXT = the prog-station control-mark value + the per-layer config layout.
  ★★★ DECISIVE (runtime trace): the qemu-user load base is STABLE per-setup (0x40a16000), so traced
  `ReadConfigDataDir` with `-d in_asm -dfilter`. It runs `0x2150a0–0x215280` then JUMPS to `0x215504`,
  SKIPPING the registration loop (`ReplacePath@0x215325`/`ConfigDataFile`/`DataFile@0x215373` never execute)
  → `CntDataFiles=0`. The skip is `0x215283: test %al; je 0x215588` = **`CfgServer::ReadDir` returned FALSE**.
  ReadDir@0x214140 → `PathName(0,LayerNr)@0x243380` → `FSystemPathname::IsAFile()` → `0x21421e je .cold`.
  PathName → `DataStore::RetrieveLayer(LayerNr)@0x241db0` then reads layer +0x54(array)/+0x58(count). ⇒ THE
  GATE: the **DataStore layer is EMPTY/MISSING**, so `PathName(0)` is invalid → `IsAFile` false → ReadDir
  bails → the jhDataFiles loop is skipped — regardless of encfs/productid/file-presence. So the real fix is
  the LAYER SETUP: the layers (SYSTEM/OEM/USR) must be created+populated in the DataStore (`DataStore::
  AddLayer`) before `ReadConfigDataDir`; that depends on the control-mark→`GetOptionTable`→layers or a
  config-init step. Chain pinned end-to-end: ReadConfigDataSet→ReadConfigDataDir→ReadDir→PathName→
  RetrieveLayer(EMPTY)→IsAFile=false→loop skipped. NEXT: find `DataStore::AddLayer`'s caller + what populates
  the layer. (Method: base stable per identical-setup → `-d in_asm -dfilter` traces are viable.)
  ★ PROGRESS: with the productid confs provided, ConfigServer NOW **stats 53 config files** in
  `/mnt/sys/config/jh_int` (`newfstatat` OK on `tnc.cfg`/`ChannelCfg.atr`/`GlobalSystemCfg.atr`/…) — so the
  productid genuinely unblocks the IsAFile/SetupDirInfo stating path (it WAS necessary). BUT they're STAT-ed,
  never OPENED (0 `openat` on data files), IPO still fails `-k=NC`. New clue: strace shows UNRESOLVED path
  VARIABLES `%SYS%/config/layout/{uniquenumbers,measureunittable}.xml`, `%OEM%/config/version.cfg`,
  `%OEM%/_mpupdate/plce.zip` stat-ed LITERALLY (=ENOENT) — `ConfigHelper::ReplacePath` is NOT substituting
  `%SYS%`/`%OEM%` (distinct from the `SYS:\…` volume form which DOES resolve to jh_int). cwd symlinks
  `"%SYS%"`→/mnt/sys didn't take (needs ReplacePath subst, not a literal dir). Two remaining gates: (1) the
  `%SYS%`/`%OEM%` ReplacePath substitution (layout/oem loads fail→likely abort), (2) data files stat-ed but
  not OPENED (ReadDataFiles→ReadHeader gated, perhaps by the %VAR% abort). The 53 stats (SetupDirInfo path)
  vs runtime-trace "ReadDir returns false" (ReadConfigDataDir path) = multiple code paths.
  ★ RE'd `ConfigHelper::ReplacePath` (it's in **libbackend-server.so** @0x1a390, 1-arg / @0x1a430 3-arg):
  it substitutes **`%oemPath%`/`%usrPath%`** (calls `FFallback::Apply(Volume,…)`/`FSystemPathname::sys()`/
  `FUserToTicket::Ticket`) — NOT `%SYS%`/`%OEM%`. The strace `%OEM%/config/version.cfg` is a SEPARATE literal
  template; those `%SYS%`/`%OEM%` paths are secondary config (layout XML, OEM version), substituted by a
  different mechanism, and are NOT the channel config — so the `%VAR%` lead is a SIDE ISSUE, not the gate.
  ⇒ The real blocker stands: the **load path** — ReadConfigDataDir's `ReadDir`→`PathName(0,layer)` returns an
  invalid path (empty layer file-array OR LayerNr mismatch vs step-1 `DataFile`) → `IsAFile` false → loop
  skipped → data files never OPENED. The productid unblocked SetupDirInfo's STATING (53 files) but a DIFFERENT
  code path than the load. NEXT: trace step-1 `DataFile` LayerNr vs ReadDir's PathName LayerNr (the empty-array
  cause); the layers exist but their file-array is empty for the load path. Productid DONE; stating WORKS; the
  load is gated on the empty layer-file-array (not the %VAR%).
  ★ ReadConfigDataDir@0x2150a0 is PER-CLIENT: its prologue does `_Rb_tree<astring,Client>::find` on the CLIENT
  MAP (key = member -0x10c8(esi)) BEFORE the layer/file work. So the empty layer-array is bound to per-client
  config state that standalone ConfigServer (no MMI/AppStartMP constellation to populate clients+layers)
  doesn't have. ⇒ HONEST: blocker #6 is the documented MULTI-COMPONENT config frontier — per-client config +
  DataStore layers + registration + productid + encfs + channel load — pinned PRECISELY (empty layer
  file-array in the per-client load path) but NOT completable incrementally; each gate reveals another
  (encfs→productid→layer→per-client). Solid wins: #5 connect + productid synth + config-file stating + gate
  pinned. The full-system qemu path (real boot populates clients/layers/productid) is the route to a FULLY
  running control. Track B reached the config-subsystem frontier.
  ★ EMPIRICAL CONFIRMATION (binary-patch test): NOP'd the gate branch `0x215285 je 0x215588` in a copy of
  libConfigSystem.so (LD_PRELOAD, same soname) to FORCE the registration loop to run past ReadDir-false.
  Result: STILL 0 data files opened, IPO still fails -k=NC, no crash. ⇒ the gate is NOT the single branch —
  forced past it, the loop STILL can't register/load because the underlying per-client/layer state (the
  DataStore layer's file-array) is empty. Bypassing the branch doesn't conjure the populated layer the loop
  needs. This CONFIRMS the config-data load needs the multi-process constellation's per-client/layer state,
  not a code-path tweak. Definitive: Track B's userspace emulator carries the control to the config frontier
  (connect + productid + stating) but the data load requires the full-system boot's state.
  ★ CONSTELLATION PATH (the way to populate that state) — DEMONSTRATED under the emulator on ARM64: AppStartMP
  (the process manager that writes the productid + spawns the constellation) blocked at "PLIB++ waiting for
  X-Server" → provided **Xvfb** (`:99`, native ARM64) → passed; then "PLIB++ waiting for X-WindowManager" →
  provided **openbox** (twm fails on missing fonts; openbox uses DISPLAY env not --display) → passed. With
  X+WM, AppStartMP now SPAWNS the constellation — forks `heuseradmin` + children which fail
  `Cannot connect to stream socket: Connection refused` (peer servers not up). So under qemu-i386+emulator+
  Xvfb+openbox, AppStartMP runs and reaches the constellation-spawn stage, but the children need the FULL set
  of HeROS servers wired up (heusrv/the message bus/the config server/the Qt MMI). The productid cache is
  written only once that constellation comes up — so it's still absent. ⇒ the documented full-GUI-boot
  constellation IS reachable as a path on ARM64 (X+WM provided) but completing it = bringing up every server
  + the Qt MMI = the documented infeasible/legally-barred ceiling. NEXT (full-boot path): start the HeROS
  service constellation (heusrv etc.) so AppStartMP's children wire up + the productid/layers populate.
  ★★★ COMPLETE SCOPE (batch/TNC640heros.txt = AppStartMP's constellation definition): the full control is
  **30 subsystems / 92 processes** — winmgr, SkManager, prom, evtserver, observer, hwserver, **ConfigServer**,
  dnc, SqlServer, flserver, HotPlugServer, sif, HelpServer, DialogServer, SharedMemServer, TaskServer,
  Workset, ifsDiagnosis, calcprocess, ConfigEditor, TableUpdtr, QsTncKeyboard/touchkeys, graphics,
  ChannelManager, Fred, ContourGraphics, TableEdit, texteditor, Pgm_Mgt, plcdiagnose, simipo/simplc/geochain,
  StatPosDisplay, TaskRunner, startup, **HrMmi.elf** (the main Qt MMI), **ipo.elf**/**ipo_progstation.elf**
  (the NCK), ipo_export, … So "fully run the control" = boot ALL 92 processes (each its own qemu-i386 +
  heroscall-emulator instance) wired together, culminating in the Qt MMI HrMmi.elf. That IS the documented
  full-system/GUI boot — feasible only up to the Qt MMI (the infeasible/legally-barred ceiling). Track B
  (userspace emulator) is proven to carry the INDIVIDUAL processes (NCK, ConfigServer) through RTOS/kernel
  init + connect + the config frontier, and the orchestrator AppStartMP RUNS on ARM64 (Xvfb+openbox) and
  spawns the constellation — but booting all 92 + the Qt MMI is the full-system path, not an incremental
  emulator step. This is the genuine, mapped endpoint of Track B.
  ★ BOOT-ORDER dependency confirmed empirically: binfmt_misc IS registered+enabled for qemu-i386 (flags POF,
  /usr/bin/qemu-i386), so i386 children auto-launch under qemu (set `QEMU_LD_PREFIX=$rootfs` so they find the
  sysroot). With Xvfb+openbox+binfmt, AppStartMP forks `heuseradmin`, which STALLS on `Cannot connect to
  stream socket: Connection refused` — it needs **`heuserver`** (`$rootfs/usr/sbin/heuserver`, the HeROS
  user/login server), a SYSTEM SERVICE the real boot starts via `/etc/init.d` BEFORE AppStartMP. So the full
  boot = the HeROS init scripts + system services (heuserver, message bus, …) + AppStartMP + the 92-process
  constellation + the Qt MMI — i.e. replicating the ENTIRE HeROS boot process-by-process under qemu-user,
  each service gating the next down to HrMmi.elf. That is definitively the full-system path (the documented
  qemu-system-x86_64 route boots all of it natively), not an incremental userspace-emulator step. Track B's
  proven reach: individual processes through RTOS/kernel init + connect + config frontier, and the
  orchestrator launching under X+WM. The full multi-process+GUI boot is the documented ceiling.
  ★★★ DECISIVE BOUNDARY (empirical): tried to start `heuserver -d` (the user/login server AppStartMP's
  heuseradmin needs). It CRASHES qemu-user: `ERROR:accel/tcg/cpu-exec.c:515: assertion failed:
  (cpu == current_cpu)` — a qemu-USER limitation (its per-process threading/signal model), and it also
  needs to write system files (`/etc/security/group.conf`). So the HeROS SYSTEM SERVICES cannot run under
  per-process qemu-user at all. The init.d boot is ~40+ services (dbus, heros, heros-auth-daemon, hessrv,
  heuinput, heuseradmin, … then applaunch→AppStartMP). ⇒ PROVEN: the full constellation boot requires
  FULL-SYSTEM emulation (`qemu-system-x86_64`, a real kernel running the whole HeROS Linux), NOT the
  userspace qemu-user + heroscall-emulator approach. Track B (userspace) definitively reaches: individual
  COMPUTE processes (NCK/ConfigServer) through RTOS/kernel init + connect + config frontier, and AppStartMP
  launching under X+WM — but the SYSTEM SERVICES + GUI boot are a full-system-emulation concern. This is the
  empirically-proven boundary between Track B (userspace) and the full-system route.
  ★ WORKAROUND-TESTED (hard limit): retried heuserver with qemu `-one-insn-per-tb` (disables TB chaining —
  the usual fix for that assertion) AND with /etc/security writable — SAME crash `cpu_exec_longjmp_cleanup:
  assertion (cpu == current_cpu)`, and heuserver dies during user/group setup (adding root to groups
  vboxsf/oem/plce, reading /etc/sysconfig) BEFORE binding the socket (0 listen/bind). So the qemu-user limit
  is NOT flag-avoidable — the HeROS system daemons' thread/signal/credential model is fundamentally
  incompatible with per-process qemu-user. (Earlier wording "the userspace heros-emulator cannot boot the
  system services" was an OVER-CLAIM — see the FEX correction below; the limit is qemu-USER-specific.)
  ★★★ CORRECTION — the boundary is qemu-USER-specific, NOT universal (2026-06-22): installed **FEX-Emu**
  (`fex-emu-armv8.0`, PPA ppa:fex-emu/fex has a candidate for Ubuntu 26.04 resolute) — a DIFFERENT i386→ARM64
  userspace translator that runs UNDER the heros-emulator (it replaces only the qemu translation layer, so
  it's still "the heros emulator on arm64"). FEX runs heuserver with **ZERO `cpu_exec` assertions** — the
  qemu-user crash is GONE. So my 5-way "hard limit" was qemu-user-specific; the HeROS system services are
  NOT fundamentally un-runnable in userspace. CAVEAT: FEX's i386 (32-bit) support segfaults (exit 139) on the
  BARE control rootfs — even a dynamic i386 busybox — because FEX needs a proper FEX-format RootFS, not the
  raw $rootfs (config: /root/.fex-emu/Config.json `{"Config":{"RootFS":"<dir>"}}`; sudo→HOME=/root). NEXT
  (the genuine open avenue): build a FEX RootFS = FEXRootFSFetcher base + the control's i386 libs overlaid
  (reconcile glibc 2.31), then run heuserver→AppStartMP→the constellation under FEX + the heros-emulator
  preload. Remaining blockers regardless of translator: writable credential env (/etc/security, /mnt/plc/etc/
  shadow, the user/group DB) + the ~40 services + the Qt MMI HrMmi.elf. So: NOT exhausted — FEX is the
  untested-but-promising path that clears the specific qemu-user crash.
  ★★★ BREAKTHROUGH (FEX runs the control's i386 binaries — 2026-06-22): solved the FEX RootFS. FEX 32-bit
  WORKS (a STATIC i386 binary printed + exit 0; a DYNAMIC i386 binary with modern glibc too). The control's
  segfault was purely the **glibc-2.31 rootfs**: glibc is backward-compatible, so a MODERN i386 glibc runs
  the 2.31-linked control binaries. Recipe: `dpkg --add-architecture i386 + apt install libc6:i386
  libstdc++6:i386`, then an **overlayfs RootFS** = `lowerdir=<modern-glibc-/lib>:<control $rootfs>` (modern
  glibc on TOP of the control tree). Result: the control's own i386 busybox runs under FEX
  (`CONTROL_BUSYBOX_OK`). Then **heuserver under FEX + the heros-emulator preload: ZERO cpu_exec assertions
  (qemu-user crash GONE), heros emulator loaded, and it runs ALL THE WAY THROUGH its credential setup** —
  group adds, config read, shadow/group.conf handling — failing only on ENVIRONMENT (read-only /mnt/plc/etc,
  absent credential DB + /etc/sysconfig/heuseradmin cfg, cross-device /etc/security rename EXDEV) + a late
  segfault from the failed ops. ⇒ DEFINITIVELY: the HeROS system services ARE runnable on ARM64 via
  **FEX + the heros emulator**; the qemu-user "hard limit" is fully refuted. The remaining work is HeROS
  ENVIRONMENT setup (writable credential dirs + the user/group/shadow DB + the heuseradmin config + FEX path
  mapping so /tmp & /etc/security share a fs), then heuserver→AppStartMP→constellation. Repro: overlay
  rootfs at /tmp/fexroot; FEX config /root/.fex-emu/Config.json RootFS=/tmp/fexroot; preloads copied into
  the rootfs /lib. NEXT: set up heuserver's credential environment so it binds its socket.
- Fallback that works today: full-system `qemu-system-x86_64`/UTM (real heros.ko loads) — doc 16 §6.

### Reproduce
- **heroscall emulator on ARM64 (the actual target, runs locally — no x86_64 box needed): `emulator/run_2proc_arm64.sh`**
  via lima VM `tnc` + qemu-i386 (build the `.so` in-VM with `i686-linux-gnu-gcc`; see docs/17 §"Runs on ARM64").
  IPO + ConfigServer fully reproduce the frontier on aarch64 (cross-process futexes work under qemu-i386).
- heroscall emulator, native x86_64: `emulator/run_2proc_config.sh` / `run_nck.sh` (see `docs/17-heroscall-emulator.md`)
- Translation + dep-closure + device shim: `scripts/arm64_translate_poc.sh`
- Recompile proof: `recomp/build_and_verify.sh`

---

## Triage facts (key numbers, for orientation)
- 335 ELF objects, **ALL i386 (Intel 80386), zero x86-64** = 87 executables (`.elf`) + 248 libraries
  (`.so`). All dynamically linked, interpreter `/lib/ld-linux.so.2`, **not stripped** (symbols
  present → legible decompilation). Largest: `ipo_progstation.elf` 8.2 MB (NCK interpolator).
- Honest limit: Ghidra pseudo-C ≠ buildable source for the C++ product; recompiling the *whole*
  control is infeasible + legally barred. Decompilation's real use here = interface recon for shims;
  per-leaf-function recompile is what's been proven (see recomp tables).

## Lessons / tooling caveats (carry these forward)
- **Rosetta is x86-64-only** → it CANNOT translate this i386 control. (Relevant on macOS; on a real
  x86_64 host this whole problem disappears — see migration notes.)
- **rz-ghidra is NOT a brew formula** — use full Ghidra (`analyzeHeadless` + the post-script).
- Native `objdump` in an ARM64 lima VM can't disassemble i386 ("architecture UNKNOWN"); use
  `i686-linux-gnu-objdump`. (On x86_64, plain `objdump`/`gcc -m32` work natively.)
- Host↔lima-VM mount was READ-ONLY → built in VM `/tmp` + `limactl copy` back; patchelf ran host-side.
- **x87 fistp/fisttp** integer conversions of 80-bit intermediates near integer boundaries are NOT
  cleanly reproducible on ARM SSE; `fisttpl(inf)=0x80000000` (x87 indefinite). This is why a few FP
  fns (e.g. `FCYC_AnzahlSchichten`, `BCYC_*` originally) were excluded from the byte-identical bar.
- **Cycle libs are function-pointer-table architectures** (esp. `libEp90_Drehcyc`): most "exports"
  are runtime-registered forwarder thunks (`jmp *GOT`), NOT reimplementable. Filter real leaves by
  "has `fld`/`fmul` AND no `@plt` AND no indirect `jmp`/`call *`".
- When Ghidra's decomp ABI looks confused/pointless (e.g. a function typed `void` that actually
  tail-returns a value in `eax`/`st0`), **disassemble** — the eax/st0 passthrough tail-return and
  true arg order are recoverable from the stack-slot shuffles (`dmathe_PktAufBogen`,
  `get_einfahr_radius` were recovered this way).
- A recompile candidate must be **EXPORTED in `.dynsym`** to serve as the truth oracle — local
  symbols (`hexbyte`, `FlModAccess`, `SlowPgmGetTaskIndex`, …) are genuine machine code but not
  dynamically linkable, so they can't be diffed against.

---

## Migration notes: moving the RE work to x86_64 (2026-06-21)
**Why:** decompilation/recompilation/verification is far easier on a native x86_64 host — no qemu,
no cross-compiler, no lima VM, no read-only-mount dance.

What changes on x86_64 (vs the Apple-Silicon M2 Max setup documented above):
- The i386 control runs **natively** (32-bit on x86_64 via multilib) — no `qemu-i386`, no
  translation layer. The whole "TRANSLATION PORT ROADMAP / heroscall" story is an ARM64-specific
  concern; on x86_64 the original HeROS `heros.ko` kernel module can load for the full-system route.
- Build/verify recompiled libs with native `gcc -m32` (install `gcc-multilib` / `glibc-devel.i686`).
  No `gcc-i686-linux-gnu` cross-compiler needed; plain `objdump`/`gdb` handle i386.
- The verification target on x86_64 is the genuine i386 `.so` running **natively** as the oracle
  (still apply the same trim-NEEDED / stub-soname / neuter-ctors recipe so the leaf loads
  standalone). Byte-identical (`recomp/*/`) results should reproduce; behavioral-FP results may now
  match the oracle even MORE closely (no qemu x87 emulation in the loop) — re-run
  `build_and_verify*.sh` to confirm and adjust tolerances if anything tightens.
- Still install: Ghidra 12.1.2 + JDK 21 (headless decompile pipeline is host-arch-agnostic),
  patchelf, rizin. The `recomp/*` artifacts named `*_arm64.dylib`/`*_aarch64.so` are ARM outputs;
  regenerate x86_64/`.so` equivalents as needed (the `.text` of verified fns is genuine and unchanged).
- IDA Pro MCP tools are available in this environment (see `mcp__ida-pro-mcp__*`) — an alternative/
  complement to the Ghidra headless pipeline for the heavier decompile work on the new host.

Open work still pending (unchanged by the move): more `libEp90_Gtlib` single-field classifiers
(~40 IsGewinde-style candidates), `libplckernel` integer accessors, un-scanned libs. The recomp set
is explicitly **NOT exhausted**.

---

## x86_64 native migration COMPLETE + IDA + new work (2026-06-21) — `ssh pawel`

The migration to x86_64 (above) is **done and proven**. The host is a Ryzen Windows box reached via
`ssh pawel`; the workhorse is its **WSL2 Ubuntu 24.04**. Full mechanics in memory
`project-x86_64-native-verify` and `recomp/x86_64_native/README.md`. Highlights:

- **Native verification pipeline works (no qemu).** No-sudo 32-bit toolchain (`~/tnc/m32gcc`, deb
  `apt-get download` + `dpkg -x`), universal auto oracle-load recipe (trim non-glibc NEEDED, supply
  versioned stubs for VERNEED sonames, neuter init, weak `ret` stub for unversioned proprietary
  syms), tolerant comparator (`recomp/x86_64_native/{nverify.sh,fpdiff.py}`).
- **All 25 prior recomp libs re-validated natively:** 22 byte-IDENTICAL (same SHA-256) + 3
  FP-EQUIVALENT (anfahr/dmathe/geolib). IMPORTANT correction: the M2 "0 ULP" FP claims were a
  **qemu x87-emulation artifact**; on real x87 hardware the FP-geometry libs differ by a few ULP —
  max **relative** error ~1e-14 (negligible, sub-femtometer). `file` lib has one cosmetic OOB-read
  harness artifact in its negative bit-index sweep (84/85 rows exact).
- **IDA Pro (idalib 9.2) works directly** via the mrexodia venv python (`ida_list.py`/`ida_decomp.py`
  in `D:\TNC\ida\`); the `mcp__ida-pro-mcp__*` tools wired to the Mac session do NOT connect.
- **NEW: `recomp/gtlib2/` — 13 new `GTFIND_*` classifiers** decompiled with IDA off libEp90_Gtlib.so,
  reimplemented via the per-arch named-field-struct technique, verified **byte-IDENTICAL** (same
  SHA-256, 46080 vectors, native i386 oracle vs native x86_64 rebuild). ARM64 deliverables built:
  `libEp90_Gtlib2_arm64.dylib` (macOS) + `libEp90_Gtlib2_aarch64.so` (Linux, via no-sudo
  `~/tnc/a64gcc` cross-compiler). Functions: IsAbflach/IsMehrkant/IsMuster/IsFigur/IsBohrung(akopf)/
  IsRohr/IsStange/IsTasche/IsRucksackTyp/IsGeoKomplett/IsGeoError/IsLine/IsCirc. Skipped HasRuck
  (IDA-garbled sparse bitmask) + IsHorLine/IsVertLine (call non-leaf `stg_element`).
- **NEW: `recomp/geometri2/` — 2 new coordinate-type classifiers** (`IsPolaresLaengenInkrement`,
  `IsPolaresWinkelInkrement`) completing the libEp90_Geometri family; reimplemented as flat
  dword-array readers (mask selects field idx 54/55/60/61, gated by 22/23, `&0x126 == K`),
  verified **byte-IDENTICAL** (5 fns incl. the 3 prior, 1344 vectors). ARM64 deliverables built.
- **More new batches (all IDA-decompiled, native-verified IDENTICAL, ARM64 built, committed):**
  `aeplib` (6 flat-field: SchlittenInKanal/MehrSpindler/chk_zustellung/VorschubTyp/ElementNichtBearbeiten/
  set_ovsi_0), `aeplib2` (3 Bam list-mutators, per-arch list), `dcsiface` (5 DcsInterface:: flat-this:
  _cfgYAxis/_isAxisAvailable[Ch]/KernOpenSpm/KernOpenWkz), `spurgen` (SwapN buffer-reverse, Box_erweitern
  bbox min/max), `geocontours` (6 libgeolibcontours flat-this predicates incl. self-ptr PocketsDefined),
  `geoxcontour` (16 libgeoextendedcontour accessors: ValueRange<uint|double> min/max/span/empty/valid +
  SplittableValueRange getters + FixedGridHash cell_*), `geoxcontour2` (12 setters: CleaningGroup fluent
  setters + ValueRange set_min/set_max).
- **IDA leaf-scanner (`D:\TNC\ida\ida_scan.py`):** lists exported leaf candidates (size 7-400, no internal
  callees — libc/import/thunk callees allowed). KEY: high-level C++ libs (libtnc/libGeoModule/libPlc*/
  libStartUpCtrl/etc.) are leaf-POOR (orchestration); the leaf-RICH libs are the low-level computational
  ones — esp. **libgeolibcontours (136 cand) and libgeoextendedcontour (215 cand)** still have many more.
- **Project total: 270 verified functions** (started this migration at 200). Still NOT exhausted.
- Deferred (need disasm or are non-leaves): GTFIND_HasRuck (garbled bitmask), GeometryTools::
  is_value_inside_range (garbled FP), is_consistent family (call externals), SplittableValueRange::
  set_range/set_number_of_samples (cold paths). Build helper: `recomp/x86_64_native/build_arm64.sh`.

### heuserver user-admin DB schema (decompiled 2026-06-22, the FEX-path next gate)
Decompiled libheusercfg.so (work/re/out/libheusercfg.decomp.c, 8807 lines, Ghidra). heuserver's
/etc/sysconfig/heuseradmin/heuseradmin.cfg is a GKeyFile permission model with sections:
[Global] (Active/Anonymous/Domain), [Roles], [Permissions], [Rights], [LegacyRoles] (NC/PLC/HEROS),
[FunctionUsers] (PWTYPEDEFAULT/PWTYPEOEM/PAMPYTHON/OEMPYTHON + per-user keys), [PlcModule9285],
[Textdomain] (DIRNAME/DOMAIN). The role→permission→rights model + the function-user/password tables are
the user-admin DB CONTENT (install-generated, internally coherent), so a syntactically-complete config
still needs valid model content to pass heuserver's validation + then the writable credential env
(/etc/group GIDs, /mnt/plc/etc/shadow, /etc/security). This is the decompiled artifact for constructing
the DB; the FEX path (control binaries run on ARM64) makes building it the genuine next sub-project.

### heuserver CORRECTION: it SELF-GENERATES the config (2026-06-22) — blocker is the writable env, not the DB
Decompiled libheusercfg shows heuserver `g_key_file_save_to_file`s /etc/sysconfig/heuseradmin/heuseradmin.cfg
("#Auto-generated by heuserver; Do not edit", decomp line 6050) — i.e. heuserver CREATES the default
user-admin DB itself (the NC/PLC/HEROS role/permission model + function-users), it does NOT need an
install-supplied config. So the earlier framing ("construct the coherent permission DB") was wrong; the real
heuserver gate is the WRITABLE CREDENTIAL ENVIRONMENT for its self-init writes: /etc/sysconfig/heuseradmin/,
/etc/security/group.conf (EXDEV — heuserver renames /tmp/__group.conf.new there; needs same-fs, e.g.
/etc/security -> a host /tmp symlink under FEX), /mnt/plc/etc/shadow, /etc/passwd|group|shadow, + the keyfile.
Under FEX the testing is noisy (the preload loads inconsistently; foreground exits 1 with empty output vs -d
segfaulting in the daemon/fork path) — so the next step is a clean, fully-writable same-fs credential env +
stable preload so heuserver self-initializes and binds. This is more tractable than a permission-model build
but still gated by the FEX env plumbing. Path stays: heuserver self-init+bind -> AppStartMP -> constellation.

### ★★★ heuserver RUNS its full credential setup under FEX (2026-06-22) — root-check + emulator solved ★★★
Got heuserver from "no output / silent exit" to running its COMPLETE credential provisioning observably on
ARM64, via three fixes:
  1. **Run as the UNPRIVILEGED user, not sudo.** FEX runs the control's i386 binaries fine as my user, but
     NOT under sudo — the lima VM's uid-501 host-mapping is unresolvable (`sudo: user 'current user' not
     found`), which breaks sudo + permission-dependent paths. (Static/dynamic i386 verified working as my
     user; sudo runs silently fail.)
  2. **emulator/fakeroot.c** (new LD_PRELOAD, loaded FIRST): geteuid/getuid->0 so heuserver passes its
     `Only root can run heuserver!` check; chown/chmod/setgroups->0 (no-op the privileged ops). Build:
     i686-linux-gnu-gcc -shared -fPIC -O2 -o fakeroot.so emulator/fakeroot.c.
  3. **Fresh /dev/shm names in heros_rtos.c** (sed heros_rtos_ctl->hrctlU501, heros_reg_->hregU501_) so the
     unprivileged user creates its own control segment (the old one was a root-owned 403MB 0600 leftover,
     EACCES; sudo couldn't remove it). [rtos] control segment created -> the emulator now inits.
With this + a FEX overlay rootfs, heuserver runs FULLY: parses the NC/PLC/HEROS legacy roles, provisions
function-users (addgroup/adduser via busybox symlinks), creates /etc/netgroup, sets file perms, GENERATES
/etc/sysconfig/heuseradmin/heuseradmin.cfg. This is the FURTHEST heuserver has reached — its actual setup.
REMAINING (one blocker): file WRITES fail (changeOemPasswd /etc/passwd.new, the keyfile temp, /tmp/
__group.conf.new, /etc/security/groups) = "Permission denied", because the overlay/virtiofs writes are
gated by the SAME unresolvable-uid-501 degradation (my-user-owned overlay upper still denies writes; the
virtiofs lowerdir $R is owned by the unresolvable uid 501, so overlayfs permission checks fail). This is a
VM-infrastructure degradation, NOT the emulator: a FRESH VM (where uid 501 resolves -> real root/sudo works,
or the rootfs isn't a virtiofs mount) lets the writes succeed and heuserver bind. NEXT: fresh VM/environment
-> heuserver self-init+bind -> heuseradmin connects -> AppStartMP -> the constellation, all under FEX.

### heuserver bind: blocked by VM-degradation stuck files (2026-06-22) — local writable rootfs WORKS for /etc
Built a LOCAL my-user-owned rootfs (no overlay/virtiofs/sudo) to make heuserver's writes succeed:
closure-trace heuserver's NEEDED libs (libheusercfg/libjhvolume/libpam/libglib-2.0/libcrypto/libhenetstat
+ transitive, ~25 libs) from work/target/rootfs into /var/tmp/lr/lib (ext4, 90G free; /tmp is tmpfs/RAM
so use /var/tmp), + busybox + the modern i386 glibc on top + the preloads + busybox helper symlinks +
writable /etc. FEX RootFS=/var/tmp/lr. RESULT: heuserver runs its FULL setup and **/etc writes now succeed**
("Create new /etc/netgroup" works) -- the local writable rootfs solved the overlay-permission wall.
REMAINING (one VM artifact): heuserver hardcodes /tmp/__group.conf.new; **FEX maps the guest /tmp to the
HOST /tmp** (verified: a pre-placed /var/tmp/lr/tmp/__group.conf.new is ignored), and host
/tmp/__group.conf.new is a STUCK root-owned file (1969B, from an earlier WORKING-sudo run at 09:47) that my
user cannot remove (sticky /tmp + sudo broken + userns blocked: uid_map EPERM). Same for the 403MB root-owned
/dev/shm/heros_rtos_ctl. These leftover-root files (from before the uid-501 drift) block the my-user runs and
can't be cleared without root. ⇒ heuserver is ONE clean step from binding: a FRESH VM (clears /tmp + /dev/shm,
restores uid-501 so sudo/root works) lets heuserver complete + bind. All the hard parts are solved (run as
user, fakeroot root-check, fresh-shm emulator, local writable rootfs, /etc writes); only the stuck-file VM
artifact remains. NEXT: fresh VM -> heuserver binds -> heuseradmin -> AppStartMP -> constellation under FEX.

### ★★★ heuserver SETUP COMPLETES under FEX on ARM64 — VM restart recovered the env (2026-06-22) ★★★
The mid-session VM degradation (uid-501 unresolvable -> sudo broken; stuck root-owned /tmp + /dev/shm
files) was cleared by `limactl restart tnc`: uid-501 resolves again, `sudo whoami`->root, /tmp + /dev/shm
clean, /var/tmp/lr (local rootfs) preserved. With REAL root restored, heuserver runs its full setup AND
its writes complete. Last blocker fixed: heuserver writes /tmp/__group.conf.new then rename()s it to
/etc/security/group.conf; FEX maps guest /tmp to the HOST /tmp (tmpfs) while the rootfs /etc is ext4 ->
rename()=EXDEV. **emulator/renamefix.c** (LD_PRELOAD) retries EXDEV as copy+unlink -> "Updated
/etc/security/groups". heuserver now: parses NC/PLC/HEROS roles, provisions groups, creates /etc/netgroup,
updates /etc/security/groups = its credential-DB setup DONE under FEX/ARM64.
OPEN: heuserver EXITS after "Updated /etc/security/groups" (foreground one-shot; `-d` daemonizes but the
double-fork doesn't survive FEX, and daemon()->0 didn't keep it up -> it genuinely returns after setup).
Need to determine whether heuserver is a setup one-shot (its job = provision the DB, then exit 0, and a
SEPARATE serving instance/socket comes later) or has a serve loop that aborts on a missing peer. NEXT:
check heuserver exit code + its serve mechanism (socket/listen vs heros-queue) + the init.d invocation args.
Recovery recipe (after any VM restart): rebuild preloads from emulator/*.c; FEX RootFS=/var/tmp/lr;
run heuserver as `sudo env ... LD_PRELOAD=/lib/renamefix.so:/lib/herosapi_shim.so:/lib/heros_rtos.so`.
(SUPERSEDED — see next section: drop heros_rtos.so + contain /etc; binds the socket.)

### ★★★★ heuserver BINDS 127.0.0.1:19093 under FEX on ARM64 (2026-06-22) — the heuserver gate is CLEARED ★★★★
Two fixes cracked the long-standing heuserver blocker; heuserver now runs its full credential setup,
creates `/dev/shm/_heusrv_shm`, and **binds + listens on 127.0.0.1:19093** (the accept loop blocks =
healthy server). `ss -ltnp` shows `LISTEN 127.0.0.1:19093 users:(("FEXInterpreter",...,fd=5))`.
Reproduce: `emulator/run_heuserver_fex.sh foreground` (in VM tnc); helper `emulator/heu_diag.sh`.

1. **DROP `heros_rtos.so` FROM heuserver's preloads.** The RTOS emulator (heroscall syscall(222),
   needed by the i386 NCK/IPO) **SEGFAULTS heuserver** (exit 139, right after "Updated /etc/security/
   groups", before the socket). heuserver needs ONLY `herosapi_shim.so` (fakes /dev/herosapi) +
   `renamefix.so` (EXDEV /tmp→/etc copy+unlink). With heros_rtos: crash. Without it: reaches
   `heuserver: Created stream socket` (printed only after bind+listen+fcntl all succeed → it IS bound).
   So the prior "heuserver exits after setup" was this SEGFAULT, not a one-shot — heuserver IS a real
   TCP server (decompiled main: getuid→getopt(-d)→`FUN_0001ae00` credential setup→`FUN_00014890` shm
   `/_heusrv_shm`→socket(AF_INET) bind **127.0.0.1:19093** (sa_data 4a 95 7f 00 00 01) listen→
   `if(!-d || daemon(0,1)==0)` poll/accept loop). init.d/heuseradmin runs `heuserver -d`; exit 0/2=OK,
   3=fail (`FUN_00014890`/socket failed). libheuseradmin clients connect over this TCP socket on the MC.

2. **CONTAIN heuserver's /etc writes — FEX LEAKS them to the REAL guest /etc.** ROOT CAUSE of the
   recurring "VM degradation" found + PROVEN: FEX RootFS does **NOT** redirect absolute-path /etc
   *writes* to the rootfs — a static i386 probe writing `/etc/__x` lands in the **REAL guest /etc**,
   not `/var/tmp/lr/etc`. heuserver runs as root and rewrites /etc/passwd|group|shadow|security, so an
   unguarded run **WIPES the lima user out of guest /etc/passwd** → sshd "Permission denied (publickey)"
   → VM unreachable. FIX: run heuserver inside a mount namespace with the rootfs /etc bind-mounted over
   /etc: `sudo unshare -m bash -c 'mount --make-rprivate /; mount --bind /var/tmp/lr/etc /etc; …
   FEXInterpreter …/heuserver'`. Writes land in the contained rootfs etc; a md5 guard on real
   /etc/passwd confirms it stays unchanged. (FEX is dynamic → also set
   `LD_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu` so it finds its libs past the bound /etc.)

**VM RECOVERY (this session): the guest /etc/passwd had ALREADY been corrupted** by a prior unguarded
heuserver run (the real guest passwd was wholesale-replaced with the HeROS control passwd — sys/oem/plce
/user, /bin/ash — and the lima `andreansx` user line removed; /etc/group + /etc/shadow still referenced
it). sshd rejected the (correct) key because the user no longer existed. `limactl restart`/`stop;start`
could NOT fix it (cloud-init re-provision blocked: lima regenerates cidata with a deterministic
instance-id). **Recovered via OFFLINE DISK SURGERY**: a throwaway helper lima VM (`~/.lima/_fixer.yaml`,
mounts `~/.lima/tnc` writable) → `losetup -fP ~/.lima/tnc/disk` → mount `loop0p1` (cloudimg-rootfs) →
restore `/etc/passwd` from the clean `/etc/passwd-` backup (which had `andreansx:x:501:6017:...:/home/
andreansx.guest:/bin/bash`) → umount/detach → `limactl start tnc`. SSH + sudo restored, all work intact
(`/var/tmp/lr`, FEX, toolchains). sudo grant was never lost (`/etc/sudoers.d/90-cloud-init-users`).

SERVING PATH VALIDATED + RTOS-FREE (confirmed). heuserver issues **ZERO heroscalls** (no syscall(222), no
t_ident/q_create/ev_*/m_attach in its decompile) and doesn't even open /dev/herosapi — it is a PURE socket
/credential server. So heros_rtos was never needed; it segfaults heuserver only because its syscall()/
sigaction() interposition hijacks SIGUSR1 (which heuserver uses). `emulator/heu_serve_test.sh`: a TCP client
CONNECTS to 19093, heuserver accepts it, reads the message, rejects a bad length (`Illegal data size …,
closing`), closes gracefully, and KEEPS LISTENING — no crash, RTOS-free, /etc guard SAFE. (Detail for the
real-client step: heuserver logged `pid 0 / connection (null)` — its AF_INET peer-uid extraction
(newTicketFromSocket → /proc/net/tcp → pid → uid) didn't identify the python client; a real heros client
auth will need that to resolve.) heuserver needs ONLY `renamefix.so` (+ harmless herosapi_shim).

REAL CLIENT END-TO-END HANDSHAKE VALIDATED (emulator/heu_client.c + heu_client_test.sh). A minimal C
client dlopen()s `libheuseradmin.so.1` (closure copied i386-correct into /var/tmp/lr: libglib-2.0/libpcre/
libcap — `cp -aL` to deref symlinks, else the i386 loader falls back to a 64-bit lib) and calls
**HEUTicketFromPid(getpid())**. RESULT: client logs `HEUTicketFromPid -> 0x1 (heuserver answered)`;
heuserver logs **`Client /usr/bin/FEXInterpreter was denied HEUTicketFromPid`**. So the FULL chain works:
real heros client code → connect 19093 → heuserver IDENTIFIES the peer (the python-probe `pid 0` is gone —
heuserver resolved the connecting process via /proc/PID/exe) → applies AUTHORIZATION → returns a decision.
"denied" is CORRECT (the client isn't a recognized privileged component).
★ FEX-MASKING BLOCKER SOLVED (emulator/fexunmask.c, an LD_PRELOAD for heuserver). Under FEX a client's
/proc/PID/exe = **`/usr/bin/FEXInterpreter`** (the translator), so heuserver's exe-path authorization
(`FUN_00019b70`: readlink /proc/PID/exe → `fnmatch` pattern table → priv bits) denied ALL FEX clients.
fexunmask interposes readlink(): for /proc/PID/exe it reads /proc/PID/cmdline and returns the REAL binary.
KEY (traced): **FEX rewrites cmdline argv[0] to the GUEST binary** (no "FEXInterpreter" prefix), so the
shim uses argv[0] when it's a path (else argv[1]). PROVEN: heuserver now logs the real path
(`Client /var/tmp/lr/tmp/testheuseradmin ...`) instead of FEXInterpreter. Add fexunmask.so to heuserver's
LD_PRELOAD (herosapi_shim:renamefix:fexunmask). Build: i686-linux-gnu-gcc -shared -fPIC -O2.
AUTH model fully RE'd: **HEUTicketFromPid needs priv bit 0x20** (heuserver @0x18210 line 6147:
`if ((client.priv & 0x20)==0) → "denied HEUTicketFromPid"`). priv bits come from matching the (now-real)
exe path against heuserver's pattern table (PTR_s___testheuseradmin_00027040); fnmatch flags 0x12 =
FNM_CASEFOLD|FNM_NOESCAPE (NO FNM_PATHNAME, so `*` spans `/`). Patterns (heuserver .rodata): `*/testheuseradmin`,
`/mnt/sys/heros5/bin*/*.elf`, `/usr/bin/heulaunch`, `/usr/bin/heoemuseradmin`, `…/ConfigServer*.elf`, etc.
★★ AUTHORIZATION FULLY SOLVED + GRANT DEMONSTRATED (no test hook). The priv-pattern table is STATIC in
heuserver .data @ELF 0x17040 (Ghidra 0x27040, base 0x10000): an array of {patternPtr(R_386_RELATIVE→.rodata),
privBits} pairs, NULL-terminated. Decoded it (objcopy .rodata + the reloc table). The FIRST entry
`*/testheuseradmin` has priv 0 by default, **set by the `-t <bits>` CLI flag** (main getopt 't'→FUN_00019b50→
DAT_00027044) — a TEST hook. Patterns granting **bit 0x20** (HEUTicketFromPid): **`/usr/bin/heulaunch`**
(priv 0x24) and **`/mnt/sys/heros5/bin*/Guppy*.elf`** (priv 0x120). The general `/mnt/sys/heros5/bin*/*.elf`
grants a LOWER priv (NOT 0x20) — correct (querying an arbitrary pid's ticket is sensitive, reserved for
heulaunch/Guppy). DEMONSTRATED grants 2 ways: (1) test hook — client named `testheuseradmin` + `heuserver
-t 32` → real ticket 0x4bfe648b (not the 0x1 deny sentinel); (2) **REAL pattern, no hook** — client staged
at `/usr/bin/heulaunch` → `HEUTicketFromPid -> 0x8aacd84f`, NO denial. fexunmask now also STRIPS the FEX
rootfs prefix (env FEXUNMASK_ROOTFS=/var/tmp/lr) so heuserver sees the GUEST/HeROS path (/usr/bin/heulaunch,
not /var/tmp/lr/usr/bin/heulaunch) its patterns expect. ⇒ heuserver authorizes FEX clients by their REAL
binary path; real constellation binaries (run from their real paths) get their proper privileges
AUTOMATICALLY, no config/hook needed. heuserver auth subsystem = SOLVED. Knobs in heu_client_test.sh:
UNMASK / CLIENT_REL / HEU_T / CLIENT_NAME.
★ BOOT ORDER MAPPED (etc/rc.d/rc5.d S-order) — heuserver's place + the road ahead:
`S20dbus → S23heros-auth-daemon → S40hessrv/S41hessrv2 → S60mbus → S71hepwdeamon →
**S77heuseradmin (=heuserver, NOW SOLVED)** → S78sshd → S79xstart (X server) → S81xfcestart →
**S85applaunch → AppStartMP → the 92-proc constellation → HrMmi.elf (Qt MMI)**`. So the proven heuserver
methodology (FEX + mount-ns /etc containment + the heros emulator + fexunmask for client auth) now applies
to the OTHER infra servers. Tractable next targets (each a server like heuserver): **hessrv** (S40, the
HeROS server), **mbus** (S60, the message bus), dbus/heros-auth-daemon/hepwdeamon — the prerequisites
AppStartMP's constellation children connect to. Then S79 X + S85 applaunch→AppStartMP. The full set + Qt MMI
remains the documented full-system ceiling, but heuserver proves individual services boot this way on ARM64.
★ NEXT TARGET SCOUTED — hessrv (S40, /usr/sbin/hessrv): the HeROS identity/license/password RPC server.
SunRPC service over a UNIX socket (`/var/run/hessrv/hessrv.sock`); `svc_register(HESSRVPROG,HESSRVVERS)`;
procs `hessrv_getident/getproduct/getserialnumber/testlicensegetexpirationdate/pwplceget_2_svc`. Usage
`hessrv [--init-crypto]`. **RTOS-FREE** (0 heros RTOS syms, no /dev/herosapi) — same class as heuserver, so
the proven recipe applies. Anticipated blockers: (1) `/dev/JHncmem` HeROS shm device (shim like
herosapi_shim, or optional); (2) writable `/var/run/hessrv/` for the socket (mount-ns containment); (3) RPC
registration needs rpcbind/portmapper up (or local); (4) crypto helper (hessrv_crypto_helper.c, --init-crypto).
★ hessrv RUN — device blocker SOLVED, then hits the SIK/LICENSE boundary (emulator/run_hessrv_fex.sh).
Closure copied (libcrypto/libtirpc). First blocker was `/dev/JHncmem` (the SIK shm device): "Could not
open device file / map SIK device". FIX (reusable groundwork): upgraded `herosapi_shim.c` — (1) fake
`/dev/JHncmem` (added to is_heros_dev), (2) back fakes with a **4 MB zeroed memfd** (not /dev/zero) so
`mmap(MAP_SHARED,size)` works, (3) added **`__open_2`/`__open64_2`** (hessrv opens via the FORTIFIED
`__open_2@GLIBC_2.7`, which the shim didn't override — same fortified-variant lesson as fexunmask's
`__readlink`). Result: `faking open("/dev/JHncmem") -> fd 5 (memfd 4MB)`, device opens+maps. NEXT blocker
is **`SIK: Authentification failed (iTNC)! / Could not init SIK`** — hessrv IS the LICENSE/SIK RPC server;
with a zeroed memfd there is no SIK chip to challenge-response. This is the documented **SIK/licensing
ceiling** (the dongle/demo-SIK; faking the SIK challenge-response = circumvention, legally barred) — a
FUNDAMENTALLY different gate than heuserver (which was self-contained/RTOS-free). So hessrv is license-gated,
unlike heuserver. The herosapi_shim memfd/__open_2 upgrades are reusable for other HeROS-device-mapping
servers. NEXT: pick an infra server that is NOT license-gated (mbus message bus, dbus, heros-auth-daemon)
to keep building the boot chain, or AppStartMP; hessrv itself needs the SIK (the licensing boundary).

★ MORE INFRA SERVERS SCOUTED (boot-chain progress):
- **dbus (S20, /usr/bin/dbus-daemon --system) — UP under FEX** (emulator/run_dbus_fex.sh). RTOS-free,
  no license. Binds `/run/dbus/system_bus_socket` on the FIRST try (closure libdbus-1/libexpat; needs a
  machine-id + the system.conf, both provided). Contained: mount-ns + a PRIVATE tmpfs on /run so it never
  touches the VM's own dbus (verified VM dbus untouched). It "just works" — standard daemon, no
  HeROS-specific blocker. Foundational system bus = checked off.
- **mbus (S60) = `mbussrv` — HARDWARE-GATED, skip.** init.d does `modprobe ftdi_sio` + needs
  /etc/mbus/server.xml: it's the machine FTDI USB-SERIAL bus (real serial hardware / the I/O-sim). Not
  runnable standalone.
- **heros-auth-daemon (S23, /usr/sbin/heros-auth-daemon) — candidate next.** RTOS-free, no SIK; token-based
  auth over a unix socket (/var/run/auth_daemon/auth-daemon-srv.sock), uses dbus (now up) + FUSE
  (/var/run/auth_daemon/fs_mount/) + sssd (hepampol_sssd.conf). FUSE/sssd may complicate it.
- **heros-auth-daemon (S23) — LOADS + INITS under FEX, doesn't persist standalone** (emulator/run_authd_fex.sh).
  Big win on the closure: its heavy deps (libQt5Core, libprotobuf, libfuse, libicu*, libstdc++, libpcre2-16)
  all copy + run under FEX. It reads its config (`-c .../daemon.conf`, tolerates missing sections → defaults),
  parses AD/LDAP/secrets, and `-d` logs "Daemonizing process" — then gracefully "Stopping daemon / Stopped
  plugins / Updating all currently known secrets", binds NO socket. (`-v` = version 4.1.2, not verbose.) No
  captured error → the daemonized child either doesn't survive FEX's double-fork or an init condition is unmet
  — most likely the **FUSE token-filesystem mount** (`fuse_mountpoint /mnt/auth_daemon`; FUSE under FEX/qemu
  is the tracker's known-hard area) or a missing serve peer. Deeper dig than the clean wins; deferred.

BOOT-CHAIN SERVER SWEEP (this session) — heuserver methodology applied across the infra servers:
| svc | order | result |
|---|---|---|
| dbus | S20 | **UP** — binds system bus socket, clean (standard daemon) |
| heros-auth-daemon | S23 | **UP** (after FUSE win) — 2 FUSE mounts (certs + token fs) + binds srv_socket; needed the real daemon.conf |
| hessrv | S40 | device blocker SOLVED (memfd /dev/JHncmem), then **SIK/license boundary** |
| mbus | S60 | **hardware-gated** (FTDI serial / I/O-sim) — skip |
| heuserver | S77 | **FULLY SOLVED** — binds+serves+auth+fexunmask (the deep win) |
Pattern: some servers solve cleanly (heuserver, dbus); others hit hard boundaries (SIK license, FTDI hardware,
FUSE). NEXT options: (a) the FUSE-under-FEX problem (unblocks heros-auth-daemon + the encfs config store);
(b) attempt AppStartMP now heuserver+dbus are up (integration test → next real constellation blocker);
(c) more compute servers in the heuserver class (RTOS-free, self-contained).

★★★ FUSE WORKS UNDER FEX (2026-06-22) — refutes the earlier "encfs/FUSE fails under qemu" conclusion.
`emulator/run_fuse_test.sh`: the control's own i386 **encfs** mounts a FUSE filesystem under FEX, encrypts
a file (plaintext `hello-fuse-fex` → encrypted name `mvzrq09bdgQr3HDzX,BBEPes` in the source dir), and
round-trips it back through the decrypted view. `mount` shows `encfs on /tmp/dec type fuse.encfs`. So FEX
correctly translates the whole FUSE protocol (i386 encfs forks fusermount → mount() syscall + passes the
/dev/fuse fd via SCM_RIGHTS → encfs serves FUSE reqs over /dev/fuse) — qemu-user could NOT, FEX CAN.
Recipe: control's i386 encfs+fusermount+closure (libfuse/libssl/librlog/...) in $R; mount-ns as root with
/dev/fuse present; `printf pass | FEXInterpreter encfs --standard -S -f <src> <mnt>` (fusermount in PATH).
⇒ UNBLOCKS: (1) **heros-auth-daemon — NOW UP** (FUSE win applied): with the real daemon.conf (the empty one
gave "No daemon section" → no socket), it FUSE-mounts BOTH `/run/auth_daemon/certs` (cert store) and
`/run/auth_daemon/fs_mount` (token fs) and binds its `auth-daemon-srv.sock` (a unix DATAGRAM socket). The
[plugin_schlegel]/[plugin_eks] sections are HARDWARE RFID/key-switch readers (/dev/schlegel_rfid,
/dev/euchner_eks0) — optional, omitted (degrade w/o hardware). So 3 servers now run under FEX: dbus(S20),
auth-daemon(S23), heuserver(S77). (2) the **ConfigServer encfs config store** (blocker #6: `/mnt/sys/config/
jh_int` is an encfs mount; the "encfs fails under qemu / FUSE-unshare" sub-blocker is removed under FEX — the
remaining config gates were productid/layer/SIK, not FUSE). NEXT (one-by-one): the config store under FEX, or
AppStartMP (integration: heuserver+dbus+auth-daemon are now up).

★★★ heros_rtos (the HeROS RTOS emulator) WORKS UNDER FEX (2026-06-22) — the UNIFICATION.
`emulator/rtos_probe.c` (minimal heroscall ISSUER) under FEX + heros_rtos: `Sys_getenv(SYS) ret=0
out="/tmp/s"` and `T_ident(self) -> tid=256`. So the RTOS emulator's core heroscall path runs under FEX —
syscall(222) interposition, the /dev/shm control-segment init, Sys_getenv (env value), T_ident (task id).
⇒ FEX runs BOTH halves of the control on ARM64: the SYSTEM SERVICES (where qemu-USER crashed with
cpu_exec asserts — heuserver/dbus/auth-daemon, all now up under FEX) AND the RTOS COMPUTE processes
(NCK/ConfigServer, via heros_rtos — previously only under qemu-i386). One translator for the whole control,
faster + free of the qemu-user thread/signal limits. (heuserver crashed *with* heros_rtos only because it
is RTOS-FREE and installs its own SIGUSR1 handler that collides with heros_rtos's async-signal carrier — a
specific conflict, not a general failure; RTOS binaries that need heros_rtos work.) The heroscall-emulator
track (ConfigServer/IPO → the config #6 frontier, run under qemu-i386 in run_2proc_arm64.sh) can now move
to FEX. NEXT: run ConfigServer/IPO under FEX + heros_rtos (full RTOS: queues/events/sems/cross-proc futexes)
to confirm the full compute track + reproduce the config frontier under FEX.
(`heros5/bin/AppStartMP.elf`, needs Xvfb+openbox) forks heuseradmin which previously got "Connection
refused" — now heuserver is up. Full constellation = documented full-system/GUI ceiling. ALWAYS run
heuserver CONTAINED (mount-ns) — unguarded = re-corrupts the VM. Recovery recipe (after VM restart):
rebuild preloads; FEX RootFS=/var/tmp/lr; `bash emulator/run_heuserver_fex.sh foreground`.
