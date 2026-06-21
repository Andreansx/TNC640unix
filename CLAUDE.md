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

## Recompiled to native ARM64 + verified equivalent (`recomp/`) — 27 libraries, 200 functions
### (14 byte-identical libraries / 88 fns below; 13 behavioral-equivalence libraries / 112 fns in the next table)

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

### Phase 2 — Build the LD_PRELOAD heroscall emulator  ← NEXT
- [ ] 2.1 Skeleton: interpose `syscall()`, dispatch on `cmd & 0xffff`, decode each param struct from `heros_ko.decomp.c` handlers
- [ ] 2.2 `Sys_getenv` — return correct HeROS env values. **OPEN QUESTION: what values?** Find them (HeROS guestproperty `/HEIDENHAIN/CFG/*`, init scripts, or SYS/PLC/TNC partition paths). Write value into the user out-buffer per the struct layout.
- [ ] 2.3 `T_ident(self)` → return non-zero tid; `Sm_create`/`Q_create`/`T_name` → success + fake handle
- [ ] 2.4 Re-run NCK, get past `PciHardware::Exception`, capture blocker #3

### Phase 3 — Iterate the blocker chain to a running control
- [ ] message bus (`libGMessage*`), config, FUSE backends, device nodes, X/Qt MMI
- [ ] each: understand → shim → advance → log

### Known blockers (live)
- **#1 `/dev/herosapi` open** — PASSED via `work/re/shim/herosapi_shim.c` (LD_PRELOAD device stub)
- **#2 `heroscall` syscall 222** — MECHANISM SOLVED: issued via libc `syscall()`, so LD_PRELOAD CAN catch it (`heroscall_probe.c` already intercepts all 57 calls, 0 leak to qemu). Remaining: emulate the commands (Phase 2). Control still aborts at `PciHardware::Exception` only because the stub returns empty data.
- Fallback that works today: full-system `qemu-system-x86_64`/UTM (real heros.ko loads) — doc 16 §6.

### Reproduce
- Translation + dep-closure + device shim: `scripts/arm64_translate_poc.sh`
- Recompile proof: `recomp/build_and_verify.sh`
