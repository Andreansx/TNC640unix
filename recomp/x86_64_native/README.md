# x86_64 native verification pipeline

Re-validates the `recomp/` leaf reimplementations against the genuine i386 control
`.so` files **natively on x86_64** — no qemu, no lima, no cross-compiler — and verifies
**new** leaf functions. Built/run on a Ryzen Windows box's WSL2 Ubuntu 24.04 (`ssh pawel`),
but works on any x86_64 Linux with a 32-bit toolchain.

## Why x86_64 (vs the Apple-Silicon M2 setup)
- The genuine i386 `.so` runs **natively** as the oracle (32-bit on x86_64) — no `qemu-i386`.
- The recompiled C is built with native `gcc -m32`/`gcc` and compared directly.
- Real x87 FPU in the loop: revealed that the M2's "0 ULP" FP claims were a **qemu artifact**.
  On real hardware the FP-geometry libs (anfahr/dmathe/geolib) are equivalent to a few ULP —
  max **relative** error ~1e-14 (sub-femtometer for a mm-scale CNC geometry lib). Still a
  rigorous behavioral-equivalence result; just measured against true hardware.

## Tooling (assembled with NO sudo)
- `m32gcc` — stock `gcc -m32` wrapped to use a 32-bit dev toolchain extracted from
  `apt-get download` of the `gcc-multilib` closure (`dpkg -x`, no install). Needs
  `-isystem /usr/include/x86_64-linux-gnu` for `bits/`, plus in-place rewrite of the i386
  `*.so` ld-scripts (`/lib32`→`/lib/i386-linux-gnu`, dev statics→extracted `lib32`).
- `nverify.sh <recomp_subdir> <oracle.so> [rel_tol_exp]` — the driver (here).
- `fpdiff.py` — tolerant comparator: exact for int/bool, IEEE754 doubles within rel/abs
  tolerance (default 1e-12); reports exact/within_tol/fail and **true max relative error**.
- `strip_versions.py` — neutralises orphaned symbol-version requirements (kept for reference;
  the driver instead supplies versioned stubs, which is safer than stripping DT_VERSYM).

## Universal oracle-load recipe (auto, in `nverify.sh`)
Copy the genuine `.so`, then:
1. `patchelf --set-soname X_trim.so`; `--remove-needed` every NEEDED not in
   {libc,libm,libstdc++,libgcc_s,libdl,libpthread,librt,ld-linux} **and** not referenced by a
   VERNEED;
2. for each VERNEED-referenced soname (e.g. `libheros.so.1`, `libQt5Core.so.5`): build a
   **versioned stub** with that soname + a version script defining the required version nodes &
   their symbols (exact version-name match, dedup);
3. `neuter_init.py` zeros DT_INIT/FINI[_ARRAY] (leaf fns need no global init);
4. **weak `ret` stub** (`LD_PRELOAD`) for every *unversioned* (proprietary/HeROS) UND symbol —
   real glibc/libstdc++ defs (version-tagged) override it; HeROS dtors/ctors become no-ops.
Run: `LD_PRELOAD=stub.so LD_LIBRARY_PATH=trimdir:lib32run:/lib/i386-linux-gnu ./truth`.

## Regression result (2026-06-21)
All 25 prior recomp libs re-verified natively: **22 byte-IDENTICAL** (same SHA-256) +
**3 FP-EQUIVALENT** (anfahr/dmathe/geolib, max rel ~1e-14). The lone `file` outlier is a harness
artifact — its negative bit-index `BitFieldTst` sweep does out-of-bounds reads into adjacent
memory whose layout differs i386↔x86_64; the 84 meaningful rows are exact.
