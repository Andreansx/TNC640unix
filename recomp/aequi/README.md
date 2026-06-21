# recomp/aequi — `libEp90_Aequi` leaves → native ARM64, *behaviorally* equivalent

Three leaf functions of `libEp90_Aequi.so` (equidistant / offset-curve helpers),
reimplemented natively and proven **observably equivalent** to the proprietary
i386 `.so` under `qemu-i386`.

## Reimplemented functions (3)

| Function | Behavior |
|---|---|
| `get_laengentoleranz(d1,d2)` | length-tolerance accessor — returns `d2` (identity on the 2nd arg in this build) |
| `AEQ_GetLaengentoleranz(d1,d2)` | same |
| `anz_same_level(head)` | count nodes of an `akopf` "same level" singly-linked list, walking the `next` pointer at field offset `+4` |

`anz_same_level` is a pointer chaser: the node's `next` link sits at byte offset
`+4` (i386) / `+8` (arm), so the harness builds the list **per-arch** (mirroring
the field order) and the returned integer count — which is arch-independent — is
compared. All three are C++ functions (mangled symbols), bound via `__asm__`
labels.

## Proof

`build_and_verify_aequi.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS refs
auto-stubbed, ctors neutered; no proprietary `VERNEED`) runs 115 deterministic
vectors → plain `diff`:

```
truth lines: 115   recomp lines: 115
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases
afd91fa5…  truth.txt
afd91fa5…  recomp.txt      # same SHA-256
```
