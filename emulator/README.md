# emulator/ — HeROS heroscall userspace emulator (Track B)

Run the **unmodified i386 TNC640 control** directly — natively on an x86_64 host,
or under user-mode translation on Apple-Silicon ARM64 — by emulating the HeROS
kernel API in userspace instead of loading the proprietary `heros.ko`.

Full write-up, ABI tables and the blocker chain: **`../docs/17-heroscall-emulator.md`**.

## Pieces

| File | Role |
|---|---|
| `herosapi_shim.c` | LD_PRELOAD stub for `open("/dev/herosapi")` / `/dev/events` (blocker #1) |
| `heroscall_probe.c` | first probe: logs which `heroscall` commands the control issues |
| `heroscall_probe2.c` | dumps the full 8-dword param struct per command (ABI recon) |
| `heroscall_emu.c` | the emulator: `Sys_getenv`, `T_ident`, `Sm/Q/M_create`, `M_ident`, `M_attach` |
| `run_nck.sh` | runs `ipo_progstation.elf` with the shims + the real HeROS env + argv |

## Build (i386 shared objects)

```sh
gcc -m32 -shared -fPIC -O2 -o herosapi_shim.so herosapi_shim.c    # x86_64 host w/ multilib
gcc -m32 -shared -fPIC -O2 -o heroscall_emu.so  heroscall_emu.c
# on ARM64: use a no-sudo i386 cross-gcc (see docs/16) instead of -m32
```

## Status

The NCK (`ipo_progstation.elf`) boots through blockers #1–#4 and runs its own
init — shared-memory mapping, env identity, argv/`FProcess`, IPO option parsing —
then stops at **blocker #5: the configuration subsystem** (`CfgMailslot` is a
client of a config *server* reached over a HeROS mailslot). That is the first
inherently multi-process dependency; the next step is real cross-process IPC +
running `AppStartMP.elf` (the process manager) so the constellation can start and
serve config. See the doc for details.
