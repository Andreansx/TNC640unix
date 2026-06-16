# Reference — Guest disk partition map

Read from `disk.raw` (the VMDK converted to raw). MBR partitioning, one extended partition
holding three logicals. Offsets/sizes are exact.

## MBR primary table

| # | Type | Start LBA | Sectors | Byte offset | Size | Boot |
|---|---|---|---|---|---|---|
| p1 | `0x83` Linux | 2048 | 41,943,040 | 1,048,576 | 20,480 MB | ✅ (0x80) |
| p2 | `0x83` Linux | 41,945,088 | 409,600 | 21,475,885,056 | 200 MB | — |
| p3 | `0x05` Extended | 42,354,688 | 54,114,304 | 21,685,600,256 | ~26,423 MB | — |

Logical partitions inside the extended container (exposed by `hdiutil` as s5/s6/s7):

| # | ext label | Blocks (4 KB) | Size |
|---|---|---|---|
| p5 | `SYS` | 1,572,864 | 6 GB |
| p6 | `PLC` | 1,048,576 | 4 GB |
| p7 | `TNC` | 4,142,080 | ~16 GB |

## ext volume details

| Slice | Label | "Last mounted on" | FS | Notes |
|---|---|---|---|---|
| s1 | `HEROS5` | `/` | ext4 | the OS root; created 2025‑05‑07 |
| s2 | `BOOT` | — | ext | `/boot` (kernel + GRUB) |
| s5 | `SYS` | — | ext (mounted `SYS:`) | **empty on base image** (only `lost+found`) |
| s6 | `PLC` | — | ext (mounted `PLC:`) | **empty on base image** |
| s7 | `TNC` | — | ext (mounted `TNC:`) | **empty on base image** |

**Important:** on the *base* OVA only `HEROS5` (`/`) and `BOOT` are populated. `SYS`/`PLC`/`TNC`
are empty and get filled from `setup.zip` (`target.tar.xz` + the SYS/PLC/TNC config zips) on
first install. In VirtualBox "programming station" mode, `PLC:` and `TNC:` are instead served
from **host shared folders** (see [04](../04-guest-heros5.md) and [06](../06-bridge-and-io.md));
`SYS:` stays on the disk partition.

## HEROS5 root (`/`) top level

```
bin  boot  etc  home  lib  lib64  mergelist  sbin  tftpboot  usr   (+ runtime dirs)
```

(`mergelist` is HeROS's update‑time config‑merge manifest — lists the `/etc/sysconfig/*`,
network, PKI, user‑admin, etc. files preserved across software updates.)
