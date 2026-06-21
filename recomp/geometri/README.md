# recomp/geometri — `libEp90_Geometri` coordinate classifiers → native ARM64, *behaviorally* equivalent

Three coordinate-type classifier leaves of `libEp90_Geometri.so`
(`IsPolareLaenge`, `IsCartInkrement`, `IsPolarerWinkel`) — genuine C++ functions
(mangled symbols) reimplemented natively and proven **observably equivalent** to
the proprietary i386 `.so` under `qemu-i386`.

## Reimplemented functions (3)

Each takes a coordinate **mask** and a `geotec` element, gates on two flag words
(`+0x58`, `+0x5c`), selects a per-coordinate field by mask (`0x4000→+0xd8`,
`0x8000→+0xdc`, `0x200000→+0xf0`, `0x400000→+0xf4`), and tests `field & 0x126`:

| Function | True when `field & 0x126 ==` |
|---|---|
| `IsPolareLaenge(mask, g)` | `4` (polar length) |
| `IsCartInkrement(mask, g)` | `2` (cartesian increment) |
| `IsPolarerWinkel(mask, g)` | `0x20` (polar angle) |

The three share one gate/select helper and differ only in the compare value.
All fields are read by **flat offset** (no pointer indirection), so a shared byte
buffer drives both arches; the harness sweeps masks (valid + invalid), both gate
states, and field encodings that hit and miss each value.

## Proof

`build_and_verify_geometri.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS refs
auto-stubbed, ctors neutered; no proprietary `VERNEED`) runs 720 deterministic
vectors → plain `diff`:

```
truth lines: 720   recomp lines: 720
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases
1f204109…  truth.txt
1f204109…  recomp.txt      # same SHA-256
```

The bool returns are read as `_Bool` (the i386 `sete al` leaves the upper `eax`
bytes undefined; only `al` is ABI-defined for a `bool` return).
