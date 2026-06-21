# 16 — Apple Silicon, measured: decompilation + i386→ARM64 translation

This document records what was **actually executed** on an Apple-Silicon **M2 Max**, not
what is theoretically possible. It is the empirical follow-up to the analysis in
[15-apple-silicon.md](15-apple-silicon.md), and it both **confirms** that file's
conclusions and **adds one new, important blocker** found by running the code.

> **TL;DR (measured).**
> 1. The entire control is **i386** — 335 ELF objects, **all** Intel 80386, zero x86-64.
> 2. Decompilation **works** (Ghidra headless → pseudo-C) and is legible because the
>    binaries are **not stripped** — but pseudo-C is not buildable source, so
>    "recompile the control for ARM" remains infeasible *and* legally barred. Decompilation's
>    real value here is **understanding interfaces to write shims**.
> 3. **i386 code runs on the M2 today.** Under `qemu-i386` user-mode translation inside a
>    native ARM64 Linux VM, the NCK interpolator `ipo_progstation.elf` resolves its **full
>    100-library i386 closure** and **executes its own startup code**.
> 4. It stops at one concrete blocker: the **HeROS kernel API** `/dev/herosapi` (from the
>    proprietary `heros.ko`). That's a *kernel module* — translators (qemu-user/box86/FEX)
>    are userspace-only — so it must be **shimmed (CUSE/FUSE) or stubbed**, the same
>    clean-room approach already used for `keypad.exe`.

The headline: **CPU/userspace translation is not the wall. The HeROS runtime/kernel API is.**

---

## 1. Tooling installed (Apple Silicon host)

| Tool | Purpose | Install |
|---|---|---|
| **Ghidra 12.1.2** + **OpenJDK 21** | static decompiler, driven **headless** (`analyzeHeadless`) | `brew install ghidra openjdk@21` |
| **rizin 0.8.2** | scriptable CLI disassembly / triage | `brew install rizin` |
| **patchelf** | ELF interpreter/rpath surgery | `brew install patchelf` |
| **lima 2.1.3** | native ARM64 Linux VM (Virtualization.framework) | `brew install lima` |
| **qemu-user 10.2.1** (`qemu-i386`) | i386→ARM64 user-mode translation *(inside the VM)* | `apt-get install qemu-user qemu-user-binfmt` |

Note the split: **decompilers run on the Mac host** (architecture-independent analysis);
**execution/translation runs inside an ARM64 Linux guest** (you cannot run i386 Linux ELFs
directly on macOS). Apple **Rosetta** is x86-64-only and therefore **cannot** translate this
i386 control — `qemu-i386` / `box86` / `FEX` are the engines that can.

## 2. Extraction & triage (the real inventory)

The control software is **not** in `target.tar.xz` (that's just the HeROS OS). It ships in
`prog/setup.zip` → `TNC640_SYS.{1,2,3}.zip`, which unpack to a tree rooted at `heros5/bin/`.

```
335 ELF objects total, classified by `file`:
  334  ELF 32-bit LSB shared object, Intel 80386
    1  ELF 32-bit LSB executable,    Intel 80386
  ---  ------------------------------------------
    0  x86-64    0  aarch64    0  arm
```

Breakdown: **87 executables (`.elf`) + 248 shared libraries (`.so`)**, matching the live-guest
measurement in [04-guest-heros5.md](04-guest-heros5.md). Largest executables:
`ipo_progstation.elf` (8.2 MB, the NCK interpolator), `dnc.elf` (7.2 MB), `hwserver.elf`
(6.8 MB), `skern.elf`/`plc.elf` (~2.9 MB each). **All dynamically linked**, interpreter
`/lib/ld-linux.so.2`, **not stripped** (symbols present → readable decompilation).

## 3. Decompilation — what it gives you, honestly

Pipeline (reproducible, headless, scriptable):

```
analyzeHeadless <proj> tnc640 -import <binary.so> \
  -postScript DecompileToFile.java -scriptPath work/re/scripts -deleteProject -overwrite
# -> work/re/out/<binary>.decomp.c   (one C function per Ghidra-recovered function)
```

Artifacts produced (`work/re/out/`): `libhdhinput.so` (numeric-field parsing), `liblsv2.so`
(LSV2 host protocol, ~29 k lines), `libProductId.so`, `libStartUpCtrl.so`, … Sample output —
a clean, recompilable *leaf* function with its real symbol name preserved:

```c
/* check_zt_char @ 00010c70 — a character-class membership test (bit array) */
bool check_zt_char(int param_1, char param_2) {
  uint uVar1 = (int)param_2 - 0x20;
  if ((param_1 != 0) && (uVar1 < 0x60))
    return (0x80 >> ((byte)uVar1 & 7) & (uint)*(byte *)(param_1 + (uVar1 >> 3))) != 0;
  return false;
}
```

**But this does not scale to "recompile the product,"** for the reasons in
[15-apple-silicon.md §2](15-apple-silicon.md): the interpolator is megabytes of optimized
**C++** (classes, templates, vtables, exceptions, inlining) across 87 exes + 248 libs that share
memory buses and a validated i386 ABI. Ghidra emits *pseudo-C*, not equivalent buildable
source; reconstructing it is re-implementing the control, and EU interop law (Dir. 2009/24/EC
Art. 6) forbids using decompilation to produce a "substantially similar" program anyway
(see [09-legal.md](09-legal.md)). **So decompilation here is reconnaissance for shims, not a
recompile route.** Which is exactly what §4 needs.

## 3a. Decompile → native ARM64, PROVEN equivalent (a worked instance)

The §3 ceiling is about the *whole product*. For a tractable **leaf** library the
decompile→recompile→ARM64 pipeline does produce correct native code — and we verified it:

- Target: the pure numeric accessor/validator functions of `libhdhinput.so` (13 exported
  functions: `get_pzt_*`, `check_pzt_range`, `check_zt_char`).
- Recovered to portable C (`recomp/libhdhinput_rebuilt.c`), preserving every offset/mask/branch,
  then compiled to **native Apple-Silicon arm64** (`libhdhinput_arm64.dylib`) and **native
  aarch64-Linux** (`libhdhinput_aarch64.so`).
- **Verified** by differential testing: the *same* 4000 deterministic vectors run through (a)
  the **real proprietary i386 `libhdhinput.so`** under `qemu-i386`, and (b) the native ARM64
  recompile — outputs are **byte-for-byte identical (same SHA-256)**, all branches covered.
  Reproduce with `recomp/build_and_verify.sh`.

Two branches the decompiler hid behind `regparm` calls had to be recovered from the
`check_pzt_range` jump table in the raw disassembly (type 3 clears bit 30 of the value's
magnitude; type 6 adds 1 when negative) — a good example of decompilation-plus-disassembly
producing *correct* native code, and of why pseudo-C alone isn't enough.

**The pipeline is repeatable — two further libraries, each a different code class, verified
byte-identical the same way:**

- `recomp/dintabs/` — **`libEp90_Dintabs.so`** (7 fns): HEIDENHAIN's DIN/ANSI thread-standard
  tables and the pure lookup/compare functions over them (`GetNennd`,
  `hole_din_werte_freistich_{ab,cd,ef,g}`, `NenndTblVgl`). This is **FP-heavy**, so it tests the
  x87-vs-SSE question head-on: the original runs 80-bit x87, the rebuild 64-bit NEON. It is still
  exact because `GetNennd` does no arithmetic (pure table load), the freistich scans only
  *compare* (and the harness probes `k/20.0`, hitting every table key exactly so no comparison
  sits on a rounding boundary), and `NenndTblVgl` returns a *copied* table entry rather than a
  computed value. Static tables are lifted verbatim from the `.so` by `extract_tables.py`
  (vaddr→file-offset resolved via the ELF section headers), so every returned double is
  bit-identical by construction. 7444-line differential sweep, same SHA-256.

- `recomp/plcbin/` — **`libplcbin.so`** (5 fns): a **stateful** big-endian "BIN PLC binary
  module" file parser (version detect, token-table → `uint32` info struct, the `ReadSPLCInfo`
  derived-offset arithmetic, bincode streaming, error codes). Verified on a crafted `.bin` built
  byte-identically on both sides. Two techniques worth noting: (1) the real `.so` lists ~24
  `DT_NEEDED` (the whole HeROS runtime, whose load-time ctors need the kernel API) but the 5
  functions are **libc-only**, so the i386 oracle is a **patchelf-trimmed copy** of the real `.so`
  (heavy `NEEDED` removed, soname set) — unchanged proprietary `.text`, just loadable standalone;
  (2) the opaque handle is created and consumed inside the library, so its layout need not match
  across arches (i386 4-byte vs arm64 8-byte `FILE*`) — only the `uint32` outputs are observed.

- `recomp/bohrcyc/` — **`libEp90_Bohrcyc.so`** (2 fns, *integer subset*): the first **partial**
  proof. This drilling-cycle lib is mostly FP geometry/coupled, but its pure-integer accessors
  (`BCYC_Typisiere_Werkzeug`, `BCYC_Angetr_Werkz`) are byte-identical over 2258 vectors. Worth
  noting: `Angetr_Werkz`'s i386 code uses `setbe al`/`sete dl`, which write only the low byte of the
  return register, so the upper bytes of `(t-1)` leak deterministically into the 32-bit result — the
  rebuild reproduces that leakage exactly. Its FP siblings are deliberately excluded:
  `BCYC_WinkelGleich` uses `sincos` (libm differs across implementations) and `BCYC_EntnormiereWinkel`
  produces a *computed* ±2π value (x87 80-bit→64-bit double-rounding can diverge from ARM64's direct
  64-bit rounding). That line — integer/lookup/copy is exact, *computed* FP and libm are not — is the
  practical boundary of the byte-identical method.

So the honest statement is sharper than "recompile is impossible": **per-library recompilation
to native ARM64 is achievable and verifiable for pure leaf code — demonstrated across ten
libraries / 70 functions** (integer accessors, FP table math, a file parser, error-class predicates
with exact return-register leakage, a 72-entry facility table, signed-div/mod tool-type codecs,
a stateful operand stack, C++-mangled geometry classifiers, endian/compare helpers, a bit-array
test); **it does not scale to the coupled C++ product** (§3). The leaf set is *not* exhausted —
more single-field classifiers and integer accessors remain; what is excluded is C++ class methods,
multi-level pointer chasers (32-bit stored pointers can't address a 64-bit buffer), and computed
FP/libm code (x87-vs-SSE / double-rounding), none byte-identical-verifiable. The native leaf libs
are also directly useful to the option-B port (less i386 left to translate).

**Generalised oracle recipe.** For a C++ lib whose leaf functions are libc-only but whose `.so`
drags the HeROS runtime: trim the heavy `DT_NEEDED`, satisfy the residual load-time relocations
with an auto-generated stub `.so` (carrying the depended library's *soname* + a version script when
a `HEROSLIB_500.0`/`JHVOLUMELIB_500.0` `VERNEED` survives — else glibc's `_dl_check_map_versions`
asserts), and neuter the C++ static ctors (`recomp/*/neuter_init.py` zeroes `DT_INIT/FINI[_ARRAY]`,
sound because leaf functions need no global init). The recompiled `.text` is unchanged proprietary
machine code. Candidates must be **exported in `.dynsym`** to serve as the truth oracle. Tooling:
use `i686-linux-gnu-objdump` in the VM (native `objdump` can't disassemble i386); the host↔VM mount
is read-only, so build in VM `/tmp` + `limactl copy` back, patchelf runs host-side.

See `recomp/README.md` and the per-library `recomp/*/README.md` (dintabs, plcbin, bohrcyc, errplib,
wznorm, plccond, gtlib, plcmap, file).

## 3b. Behavioral equivalence — the classes the byte-identical bar EXCLUDES

The §3a proof demands the same SHA-256, which restricts it to **pure leaves**: no computed FP (x87
80-bit vs ARM 64-bit double-rounding), no C++ class methods (`this`-layout pointer widths, x87
return registers), no multi-level pointer chasers (a 32-bit pointer stored *inside* the data can't
address a 64-bit buffer). Those three classes are reimplemented natively under a relaxed but still
rigorous standard — **observable equivalence**: identical outputs for identical inputs, *exact* for
integer/boolean returns and within a tight FP tolerance (ULP/relative + a near-zero absolute floor)
for computed doubles, measured differentially against the genuine i386 `.so` under `qemu-i386`. The
`.text` genuinely differs (we claim equivalent *behavior*, not identical *bytes*).

Measured so far — **13 libraries, 112 functions**, every suite green:

| Dir | Library | Class | Result |
|---|---|---|---|
| `bohrcyc_fp` | `libEp90_Bohrcyc` (2) | angle de-norm + sin/cos compare | 70957 vectors, doubles 0 ULP |
| `metaval` | `libtncMetaValue` (15) | static unit-conv + `this`/pImpl C++ accessors | 1283 vectors, doubles 0 ULP |
| `productid` | `libProductId` (13) | control-mark identity predicates | full-range, deterministic → same SHA-256 |
| `dmathe` | `libEp90_Dm` (22) | 2D geometry (atan/sqrt/modf) | 8306 vectors, doubles 0 ULP |
| `dkomp` | `libEp90_Dm` (23) | linked-list pointer chasers (5 families) | per-arch list, tag traversal → same SHA-256 |

Two techniques make this work past the ISA boundary. **(1) Per-arch-native objects:** for a `this`/
pointer-chasing method the harness mirrors the class FIELD ORDER and builds the object *per-arch from
identical logical inputs* — compiled i386 it reproduces the 4-byte-pointer offsets, compiled ARM64 it
lays out native 8-byte pointers; both the real method and the reimplementation then read their own
arch's correct layout (and pointer-valued returns are compared by a payload *tag*, never raw). **(2)
The `bool` low-byte contract:** i386 `bool` returns only define `al`; the upper `eax` bytes carry
`CONCAT31`/`setb` leakage that is load-address-dependent garbage, so the harness reads those
prototypes as `_Bool`. Same oracle recipe as §3a; only the verification standard changes. A notable
empirical finding: qemu's x87 emulation rounds add/sub/sincos/atan/sqrt/modf **identically** to ARM
libm for these inputs, so "behavioral" came out to **0 ULP** in practice — only sub-1e-16
catastrophic-cancellation residuals needed the absolute floor. See `recomp/{bohrcyc_fp,metaval,
productid,dmathe,dkomp}/README.md`.

## 4. Translation — i386 control code running on the M2 (measured)

Inside the ARM64 lima VM (`uname -m` → `aarch64`), with a combined sysroot
(`work/target/rootfs` = HeROS i386 OS, with the control tree grafted at `/heros5`):

**(a) Baseline — i386 userspace executes:**
```
$ qemu-i386 -L $ROOTFS $ROOTFS/bin/busybox uname -a
Linux lima-tnc ... i686 GNU      # translated process sees itself as i686, on an aarch64 kernel
```

**(b) The real product — full dependency closure resolves:**
```
$ qemu-i386 -L $ROOTFS -E LD_LIBRARY_PATH=/heros5/bin \
      $ROOTFS/lib/ld-linux.so.2 --list .../ipo_progstation.elf
  -> 100 libraries resolved, 0 not found
     libstdc++.so.6 => /usr/lib/...        (HeROS i386 libstdc++)
     liblsv2.so, libGMessageIpoInternal.so, libiocreader.so, ...  (proprietary, /heros5/bin)
```

**(c) Its own code runs — and reveals the blocker:**
```
$ qemu-i386 -L $ROOTFS -E LD_LIBRARY_PATH=/heros5/bin \
      $ROOTFS/lib/ld-linux.so.2 .../ipo_progstation.elf
  modprobe: FATAL: Module heros not found in directory /lib/modules/7.0.0-15-generic
  libheros_init: open("/dev/herosapi", O_RDONLY) failed (2)!
```

Those two lines are emitted by the **control's own i386 init code**, executing under
translation on Apple Silicon. It got through C++ static init across 100 translated libraries,
then tried to bring up the HeROS kernel interface and exited because that interface is absent.

## 5. The new blocker: the HeROS kernel API (`/dev/herosapi` / `heros.ko`)

[15-apple-silicon.md](15-apple-silicon.md) judged the proprietary *kernel* coupling to be
"essentially nil" because only `hsci.ko` is proprietary and it's unused on a programming
station. **Running the code corrects that:** there is a second, *required* coupling hit at
startup:

- `lib/modules/5.2.21-rt15-yocto-heros5-x86_64/extra/heros.ko` — proprietary **x86-64 kernel
  module** exposing a syscall-like gateway **`heroscall`** via the char devices **`/dev/herosapi`**
  and **`/dev/events`** (confirmed by `heros.ko` tracepoints `trace_event_*_heroscall_*`).
- `etc/init.d/heros` loads it; `etc/udev/rules.d/10-heros.rules` sets device modes for
  `herosapi`, `events`, `HSCI*`, `JH*`, `jhhw`.

Because `heros.ko` is a **kernel module**, no user-mode translator can run it (and being
x86-64 it can't load into an ARM kernel either). It is therefore the one component that — like
`keypad.exe` before it — must be **reimplemented/shimmed**, not translated. Options:

1. **CUSE/FUSE shim** — provide a userspace `/dev/herosapi` that answers the `heroscall`
   ioctls the programming station actually issues. Scope = reverse the `heroscall` command
   set from `heros.ko` + the `libheros_init` caller (targeted decompilation — §3's purpose).
2. **`LD_PRELOAD` interpose** — stub `libheros_init`'s `open()/ioctl()` on the device so the
   control proceeds in a "no kernel services" mode (plausible for a programming station, which
   has no servo loop / RT requirements — cf. [15 §4-B](15-apple-silicon.md)).
3. **Full-system fallback** — run the unmodified x86 HeROS image under `qemu-system-x86_64` /
   UTM, where the real `heros.ko` loads. Slower, but zero shimming (option A in doc 15).

## 5a. Shimming the first blocker — measured, and what it revealed

We built an `LD_PRELOAD` i386 stub (`work/re/shim/herosapi_shim.c`, raw syscalls so it
needs no glibc newer than HeROS's 2.31) that fakes `open("/dev/herosapi")` and answers its
`ioctl`s with success. Result (`scripts/arm64_translate_poc.sh` + the shim):

```
[herosapi_shim] faking open("/dev/herosapi") -> fd 3      <- blocker #1 PASSED
...
24545 Unknown syscall 222                                 <- control issues a custom syscall
24545 Unknown syscall 222
terminate called after throwing an instance of 'PciHardware::Exception'
qemu: uncaught target signal 6 (Aborted) - core dumped    <- blocker #2
```

So the stub **got the control past the kernel-API open**, and it ran further into hardware
bring-up — then threw `PciHardware::Exception` and aborted. The strace shows why: just before
the throw the control issues **`syscall 222`** (and earlier `407`), which `qemu-i386` reports
as *Unknown*. **i386 syscall 222 is unused in mainline Linux** — exactly the kind of free slot
a proprietary module claims. Combined with `heros.ko`'s `heroscall` tracepoints, the strong
reading is:

> `heros.ko` provides **both** the `/dev/herosapi` device **and a custom `heroscall` syscall
> (≈222)**. The control uses that syscall for hardware/PCI/RT queries; the `PciHardware` probe
> calls it, gets `-ENOSYS` under translation, and throws.

This is the key refinement over §5: the HeROS kernel coupling is **syscall-level, not just a
device node**. `LD_PRELOAD` *cannot* intercept raw `syscall()` instructions, so a userspace
shim alone can't satisfy it. Realistic ways past blocker #2:

- **Patch the translator.** Add a `heroscall` (222) handler to a `qemu-user`/FEX fork that
  emulates what `heros.ko` returns for the programming-station subset. Requires reversing the
  `heroscall` command semantics from `heros.ko` (now a well-scoped RE target).
- **Full-system route (option A).** Run the unmodified x86 HeROS under `qemu-system-x86_64`/UTM,
  where the *real* `heros.ko` loads and syscall 222 just works. No shimming; slower. This is the
  reliable path and is unblocked by everything found here.

## 6. Where this leaves the goal

- **"Decompile the libraries and recompile them to ARM64"** — *recompile* is off the table
  (scale + law), and decompilation is proven to be most useful as **interface recon for shims**.
- **"Run the control on ARM64"** — empirically **within reach**: i386 translation already
  executes the proprietary code on the M2. What remains is HeROS **runtime/kernel-API**
  integration, now reduced to a concrete first target: **`/dev/herosapi`**.

**Status of the userland-translation track (option B):** blocker #1 (`/dev/herosapi` device)
is **shimmed and passed**; blocker #2 is the **`heroscall` custom syscall (≈222)**, which a
userspace shim can't reach. Passing it needs a translator-level syscall handler derived from
`heros.ko` — a bounded but real RE task. Until that lands, **option A (full-system
`qemu-system-x86_64`/UTM)** is the route that boots the control on Apple Silicon today, because
the real `heros.ko` loads there.

Reproduce all of the above with `scripts/arm64_translate_poc.sh` (translation + dep closure +
the shim run). Decompilation artifacts are in `work/re/out/`; the shim is `work/re/shim/`.
