# libEp90_Gtlib — second batch: 13 NEW leaf classifiers

Decompiled with **IDA 9.2 (idalib)** off the genuine i386 `libEp90_Gtlib.so`, reimplemented in
portable C, verified on x86_64 with `recomp/x86_64_native/nverify.sh`. These extend the original
`recomp/gtlib/` (9 GTFIND classifiers). New here:

| Function | Kind | Logic |
|---|---|---|
| `GTFIND_IsAbflach(akopf*)`   | 3-level chain | `a->p16->p16->typ == 59` |
| `GTFIND_IsMehrkant(akopf*)`  | 3-level chain | `… == 58` |
| `GTFIND_IsMuster(akopf*)`    | 3-level chain | `… == 20` |
| `GTFIND_IsFigur(akopf*)`     | 3-level chain | leaf `typ<=0x13 && ((1<<typ)&0xD2800)` |
| `GTFIND_IsBohrung(akopf*)`   | 2-level chain | `a->p16->typ == 21` |
| `GTFIND_IsRohr(akopf*)`      | gated chain   | `f28∈{6,36}` then `a->p16->p16->typ == 6` |
| `GTFIND_IsStange(akopf*)`    | gated chain   | `f28∈{6,36}` then `… == 7` |
| `GTFIND_IsTasche(akopf*)`    | composite     | `p16 && p20 && p16->typ==1 && !Figur && !Abflach && !Mehrkant` |
| `GTFIND_IsRucksackTyp(geotec*,basvar)` | 2-level | `g->p16->typ == basvar` |
| `GTFIND_IsGeoKomplett(geotec*)` | flat | `g->typ==1 && ((g->flags>>1)&1)` |
| `GTFIND_IsGeoError(geotec*)`    | flat | `g->typ==1 && ((g->flags>>3)&1)` |
| `GTFIND_IsLine(geotec*)`        | flat | `g->typ==1 && ((g->flags>>5)&1)` |
| `GTFIND_IsCirc(geotec*)`        | flat | `g->typ==1 && ((g->flags>>6)&1)` |

**Technique — per-arch named-field struct.** The i386 code reads raw byte offsets (`+16`/`+20`
child pointers, `+28` discriminator, `+84` type, `+92` flags). `node` mirrors those as named
fields so the compiler reproduces the i386 4-byte-pointer layout under `-m32` (asserted via
`offsetof` `_Static_assert`s) and the natural 8-byte layout otherwise; functions navigate by name,
identical logic on both arches. This is how multi-level pointer-chasers (excluded from the original
byte-identical bar) are made verifiable — cf. `recomp/dkomp`, `recomp/metaval`.

**Verification:** the first 11 (without IsLine/IsCirc) verified **byte-IDENTICAL** — same SHA-256
truth==recomp over 23040 vectors, native i386 oracle vs native x86_64 rebuild, no qemu.
IsLine/IsCirc added after (same flat-flags idiom as IsGeoKomplett/IsGeoError); re-confirm with
`nverify.sh gtlib2 libEp90_Gtlib.so`.

Skipped: `GTFIND_HasRuck` (IDA-garbled sparse bitmask test — needs disassembly, not pseudo-C);
`GTFIND_IsHorLine/IsVertLine` (call a non-trivial `stg_element` helper, not leaves).
