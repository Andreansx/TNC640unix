# recomp/productid — `libProductId` identity predicates → native ARM64, *behaviorally* equivalent

Genuine C++ class methods (`ProductId::Is...`, mangled symbols) reimplemented
natively and proven **observably equivalent** to the proprietary i386 `.so`
under `qemu-i386`. The predicates classify the running control by its
**control-mark** product code; `SetControlMarkForTest(int)` writes that code, so
the harness drives every value `-2..0x40` through all predicates.

## Reimplemented methods (13)

| Method | Behavior |
|---|---|
| `SetControlMarkForTest(int)` | writes the control-mark word |
| `GetControlMark()` | the control-mark word |
| `IsTurningProduct()` | `(unsigned)(info-0xb) < 2` → `{0xb,0xc}` |
| `IsCncPilotProduct()` | `info == 0xc` |
| `IsManualPlusProduct()` | `info == 0xb` |
| `IsMillTurnProduct()` | `info == 0x10 || info == 0x16` |
| `IsAnalogProduct()` | `(unsigned)(info-6) < 3` → `{6,7,8}` |
| `IsTNC7Generation()` | `0` for the legacy set `{0,6,7,8,0xb,0xc,0xe,0x10,0x14,0x1a,0x1b}`, else `1` |
| `GetNcState()` | config global (NC state) |
| `IsExportVersion()` / `IsProgStationVersion()` | bytes of the NC-version flag word |
| `IsVirtualMachine()` | virtual-machine flag |
| `IsPhysicalMachine()` | `IsVirtualMachine() ^ 1` |

## Notes

- **Register leak:** `IsCncPilotProduct`/`IsManualPlusProduct`/`IsTurning`/
  `IsAnalog` compile to `CONCAT31(<junk>, <bool>)` on i386 (e.g.
  `mov eax,0x13f00; sete al`). These are C++ `bool` returns, so only `al` is
  ABI-defined — the harness reads them as `_Bool`. We return the clean boolean.
- **Config globals:** `GetNcState`/`IsExportVersion`/`IsProgStationVersion`/
  `IsVirtualMachine` are normally filled by the coupled `ProductId::Update()`
  from the HeROS config bus. With the static ctors neutered they sit at their
  load-time default (`0`), which both the real `.so` and the reimplementation
  reproduce; the control-mark predicates carry the exhaustive coverage.

## Proof

`build_and_verify_productid.sh` (heavy `DT_NEEDED` trimmed, unversioned HeROS
refs stubbed, ctors neutered) → the output is fully deterministic, so the proof
is a plain `diff`:

```
truth lines: 68   recomp lines: 68
RESULT: BEHAVIORALLY IDENTICAL  native ARM64 == proprietary i386 on all cases
091deb0e…  truth.txt
091deb0e…  recomp.txt      # same SHA-256
```
