# Reference — OVF / VM definition summary

Extracted from `TNCvbProg.ovf` inside `base/TNCvbProg.ova`. These are *facts about HEIDENHAIN's
VM configuration*, recorded for the port. (The OVF file itself is proprietary and not committed.)

## Identity

| Field | Value |
|---|---|
| VirtualSystem id / VM name | `HeROS5` |
| Guest OS type | `Linux_64` (`Other_64`) |
| Machine UUID | `{736e6e39-e51f-4178-9fb8-9776049f1284}` |
| Config format | `1.19-linux` |
| Product / Vendor / Version | `TNC VBox Base VM` / `HEIDENHAIN` / `5.18.4.002` |
| Snapshot folder | `Snapshots` |

## Hardware

| Item | Value |
|---|---|
| vCPU | 1 (OVF) — installer sets `JhVmCpu=2` |
| RAM | 1024 MB (OVF) — installer overrides; manual budgets ~3 GB/station |
| VRAM | 64 MB |
| Chipset | ICH9 |
| BIOS | IOAPIC enabled |
| CPU features | PAE enabled, HW‑virt large pages enabled |
| Paravirt | Legacy |
| RTC | UTC |
| Pointing device (HID) | USBTablet |
| Audio | ALSA (in+out) |

## Storage

| Item | Value |
|---|---|
| Controller | IDE, type PIIX4, 2 ports, bootable, host I/O cache on |
| Disk | `TNCvbProg-disk001.vmdk`, stream‑optimized |
| Virtual capacity | 49,392,123,904 bytes (≈ 49.4 GB / 46 GiB) |
| Disk UUID | `{fc4ef3e5-d807-4cab-87b9-dc64780765df}` |
| Boot order | 1) DVD 2) HardDisk |

## Display / remote

| Item | Value |
|---|---|
| VRDE (RDP) | enabled, `TCP/Ports = 3389` |

(Note: VBox 7.x VRDE requires Oracle's extension pack, which is **not** bundled — only JHIO is.)

## USB

Controller: **OHCI**. Device filters (auto‑passthrough by VID:PID):

| Filter | VID | PID |
|---|---|---|
| HEIDENHAIN TE 7xx | `1091` | `0730` |
| HEIDENHAIN TE 6xx | `1091` | `0630` |
| HEIDENHAIN TE 5xx | `1091` | `0530` |
| MARX USB CrypToken | `0d7a` | `0001` |
| AKS Hardlock USB 1.02 | `0529` | `0001` |

## Networking

| Adapter | Type | Mode | Host iface | MAC |
|---|---|---|---|---|
| slot 0 | E1000 (82540EM) | **Bridged** | `eno1` | `08:00:27:A8:26:74` |
| slot 1 | E1000 (82540EM) | **Host‑Only** | `vboxnet0` | `08:00:27:A7:16:7F` |
| slots 2–35 | — | declared, disabled (default NAT) | — | various `08:00:27:*` |

Two logical networks declared: `Bridged`, `HostOnly`.

## Recreating with VBoxManage (sketch for a Linux host)

```sh
VBoxManage import TNCvbProg.ova --vsys 0 --vmname HeROS5
VBoxManage modifyvm HeROS5 --cpus 2 --memory 3072 --vram 64 --chipset ich9 --pae on
VBoxManage sharedfolder add HeROS5 --name Install --hostpath /path/VM/Install --automount
VBoxManage sharedfolder add HeROS5 --name IOsim   --hostpath /path/VM/IOsim   --automount
VBoxManage sharedfolder add HeROS5 --name PLC     --hostpath /path/VM/PLC
VBoxManage sharedfolder add HeROS5 --name TNC     --hostpath /path/VM/TNC
VBoxManage guestproperty set HeROS5 /HEIDENHAIN/CFG/Display/VMSVGA 64
# … then VBoxManage startvm HeROS5
# (USB dongle passthrough, the JHIO extpack/sim, and input wiring handled separately)
```
