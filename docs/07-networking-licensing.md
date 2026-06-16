# 07 — Networking, Licensing, Demo Mode & System Requirements

Sourced from the official manual (`PGM-Platz_Virtual_en.pdf`, EN 02/2025) cross‑checked
against the OVF and binaries.

## System requirements (documented)

| Item | Requirement |
|---|---|
| Host OS | Windows 10 / 11 **64‑bit**; admin rights to install; **MS Visual C++ Redistributable** required |
| CPU | hardware virtualization **VT‑x / AMD‑V mandatory** (install aborts without it) |
| RAM | ≥ **8 GB** (≈**3 GB per running station**; TNC7: 16 GB / 6 GB each). Installer also blocks VMs with **< 1536 MB** |
| Disk | ≥ **20 GB free per station** |
| GPU | dedicated GPU with ≥ 2 GB VRAM recommended; color depth ≥ 16‑bit |
| Display | TNC 640 ≥ **1280×1024** (TNC7 ≥ 1920×1080); touchscreen supported |
| Interfaces | USB (local dongle); LAN (network license) |

**Conflicts:** Microsoft **Hyper‑V / WSL2 / virtualization‑based security** consume VT‑x and
degrade VirtualBox; the manual says to disable them, use VirtualBox's slower "software
interface," or switch to **VMware Workstation**. **VirtualBox version is pinned to the
TNCvbBase version** — updating VirtualBox requires updating TNCvbBase (here: VBox 7.1.4 ↔
TNCvbBase 5.18.4 / JHIO 4.3.0‑r6).

## Networking

From the OVF and the manual's classroom diagrams:

- **VM adapter 0 = Bridged** (`eno1`): the control gets a normal LAN IP (via DHCP in the
  classroom examples). Inside the control this is `PP eth0`.
- **VM adapter 1 = Host‑Only** (`vboxnet0`): the private PC↔control link, also used to relay a
  local license to the dongle.
- **VRDE / RDP** screen on **TCP 3389** (remote viewing).
- **Handwheel** server on **TCP 19035** (from `handwheel.exe`).
- **Network license (MARX/SmarxOS)** uses **UDP 8765** (firewall must allow it).

Documented classroom IP plan (example): license server `10.10.10.2`, instructor `10.10.10.3`,
student PC `10.10.10.8` (DHCP), student control `PP eth0` `10.10.10.108` (DHCP).

### Exchanging NC programs with the PC (4 documented ways)

1. **VirtualBox shared folder → `SF:` drive.** Add a host folder (e.g. `C:\pgmtransfer`) as a
   VBox shared folder (read‑only/auto‑mount options), restart the VM; it appears on `SF:`.
2. **The pre‑made `Install` / `IOsim` shared folders** under the VM directory (also on `SF:`).
3. **NC Share** (Control Panel → NC Share): assign a free Windows drive letter to the
   control's **`TNC:`** volume → it appears as a Windows network drive.
4. **TNCremo over the network**, preferably as a **secure (SSH‑tunnelled) connection**
   (requires **TNCremo ≥ 3.3**). When user administration is inactive the default credentials
   are **`user` / `user`**; default dirs PC `C:`, control `TNC:`.

Visible control drives: `TNC:` and `SF:` to end users; `PLC:` and `LOG:` only after a code
number / daily password. HeROS paths behind them: `/mnt/tnc`, `/mnt/sys`, `/mnt/plc`,
`/home/user`. (Note: the secure connection requires enabling password auth + SSH in the
control's HEROS settings/firewall first.)

## Licensing

HEIDENHAIN uses **USB dongles from MARX** as "software release modules." Two dongle types are
recognised (and pre‑filtered in the OVF for USB passthrough):

| Dongle | USB VID:PID |
|---|---|
| MARX **USB CrypToken** | `0d7a:0001` |
| **AKS Hardlock** USB 1.02 | `0529:0001` |

Plus the physical operating‑panel keyboards (passthrough filters): TE 7xx `1091:0730`,
TE 6xx `1091:0630`, TE 5xx `1091:0530` (HEIDENHAIN USB vendor `0x1091`).

- **The feature set is controlled by SIK option bits** (read in‑guest with `hegetsikopt`,
  viewed with `helicenseviewer`). The dongle supplies the entitlement.
- **Licenses are dongle‑bound, not PC‑bound.** You can run **multiple stations in parallel**;
  each running instance consumes one license; extras fall back to **demo/trial mode**.
- **Network licensing** uses the **SmarxOS / CBIOS Network Server** (a Windows service;
  managed by `AdminApp.exe`), clients point at the server IP via Control Panel → **Hardlock →
  "Use license Server"**, traffic on **UDP 8765**.
- License product IDs (examples from the manual): operating‑panel single station `1113967‑03`;
  virtual‑keyboard single `1113924‑xx`; network packs `1125955‑xx` (1), `1113926‑xx` (14),
  `1113928‑xx` (20).

### Demo / trial mode (no dongle)

Without a release module the station **runs in demo mode** — it boots and is operable, but:

- **≤ 100 NC‑program lines** can be run in simulation or edited, and
- **≤ 10 elements** can be selected in the CAD Viewer.

This is the important enabler for the project: **no dongle and no physical panel are required
to run it** — only the feature limits apply. (Separately, some SIK feature options are simply
unavailable on the programming station, e.g. the 3D‑mesh CAD function #152.)

### Successor note (vTNC7 / NC‑SW 20)

The newer **vTNC7 (817625‑20)** moved to **SIK2**: online license (vSIK + HEIDENHAIN online
server), USB SIK2 dongle (offline, activated via a license‑key file), or legacy MARX. Crucially
the **MARX Crypto‑Box drops to demo‑only at software version 20** — but the package in this
repo is **version 18**, where MARX is fully functional. A rental ("Mietlizenz", ≥12 months)
model also appears for vTNC7. None of this changes the v18 product here; it's context for if/when
you move to a newer NC‑SW.
