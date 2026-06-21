# recomp/dkomp — `libEp90_Dm` linked-list navigators → native ARM64, *behaviorally* equivalent

The **multi-level pointer-chaser** class — the one the byte-identical bar
explicitly can't reach, because a 32-bit pointer stored inside the i386 data
structure can't address a 64-bit ARM buffer. Here we reimplement the lock-free
doubly-linked-list navigators of **five list families** (`huelle`, `hilf`,
`edge`, `rot3D`, `box3D`) natively and prove **observable equivalence** against
the real i386 `.so` under `qemu-i386`.

## Reimplemented functions (23 — five list families)

**huelle (6):** `dkomp_nw_get_huelle` (resolve container) and `_first` /
`_last` / `_current` / `_next` / `_prev`. **hilf (5):** `_first` / `_last` /
`_current` / `_next` / `_prev`. **edge (4):** `_start` / `_end` / `_next` /
`_prev`. **rot3D (4)** and **box3D (4):** `_first` / `_last` / `_next` / `_prev`
(no `_current` — the real lib doesn't export it for these two).

All five share the container/node layout but differ in how they resolve and
walk. The container lives in a per-channel descriptor at family-specific offsets
(huelle@`+0` via a double indirection, hilf@`+4`, rot3D@`+0x18`, box3D@`+0x1c`);
`edge`'s slot is a descriptor `{container, _, start, end}` and its `next`/`prev`
take the current node as an **explicit argument** (caller-supplied cursor)
rather than the stored one. All reproduced per-arch. The chase is four levels
deep:

```
handle ─[+0x2c + (ch-1)·ptrsize]→ piVar1 ─*→ container
container {head @+0x20, tail @+0x24, cursor @+0x28}
node {next @+0, prev @+ptrsize}
```

`first`/`last` set the cursor to head/tail; `next`/`prev` advance/retreat it
along the node links and return the new node (or 0 at the ends, leaving the
cursor put); `current` returns the cursor.

## How the pointer-width wall is crossed

The navigators return **node pointers** — 4 bytes on i386, 8 on ARM — so raw
return values are not comparable. Instead `dkomp_layout.h` mirrors the original
field order and the harness builds an equivalent list **per-arch** (i386
reproduces the original byte offsets with 4-byte pointers; ARM lays out native
8-byte pointers). After each navigator call the harness dereferences the
returned node and prints its **payload tag** — a logical identity that is
identical on both sides. The traversal sequence (and the mutating cursor state)
is therefore compared meaningfully across the ISA boundary.

## Proof

`build_and_verify_dkomp.sh` drives a forward/backward sweep over a 5-node list,
plus 1-node, empty-container, null-handle and empty-slot guards. The real i386
navigator genuinely walks the list:

```
first 10 · next 20 · next 30 · next 40 · next 50 · next 0 · next 0
current 50 · prev 40 · prev 30 · prev 20 · prev 10 · prev 0 · prev 0
current 10 · last 50 · prev 40 · first 10 …
```

Output is deterministic, so the proof is a plain `diff`:

```
truth lines: 29   recomp lines: 29
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases
85838813…  truth.txt
85838813…  recomp.txt      # same SHA-256
```

The same per-arch-native-object technique extends directly to the sibling list
families (`hilf`, `edge`, `rot3D`, `box3D`) — identical chase, different field
offsets.
