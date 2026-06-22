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
