# 09 — Legal & Redistribution

> **Not legal advice.** This is an engineer's good‑faith reading to keep the project on the
> safe side. For anything you intend to publish or distribute, confirm against the actual
> license texts in the package (`LicenseAgreement.rtf` / `Lizenzbedingungen.rtf`,
> `ExtPack-license.txt`) and, if in doubt, a qualified lawyer.

## Short answers

- **Can this repository be public?** **Yes — as long as it contains only our own
  documentation/analysis and *none* of HEIDENHAIN's or Oracle's files.** That's how this repo
  is set up: the download and everything extracted from it are git‑ignored.
- **Does running it on Linux/macOS violate HEIDENHAIN's terms?** **Running** it (for your own
  use, especially in demo mode) is low‑risk. The EULA may *say* "Windows only" and may forbid
  reverse engineering, but in the **EU** you have non‑waivable rights to study and to reverse
  engineer software **for interoperability** (below). The clearly prohibited act is
  **redistributing their binaries** — which we are not doing.

## What is proprietary here (do not redistribute)

| Artifact | Owner | License signal |
|---|---|---|
| `TNCvbProg.ova` (HeROS5 + TNC 640 control SW) | Dr. Johannes Heidenhain GmbH | proprietary EULA (`LicenseAgreement.rtf`) |
| `setup.zip` / `target.tar.xz` (control rootfs, RPMs) | HEIDENHAIN (+ upstream OSS components) | mixed; HEIDENHAIN parts proprietary |
| `Heidenhain_VBoxJHIO_…vbox-extpack` | HEIDENHAIN | `ExtPack-license.txt`: *"THIS is **NO** open source or free software and strongly prohibited"* |
| `tncvbcntl.exe`, `keypad.exe`, `handwheel.exe`, `jhiosimhostd.exe`, `iosim.dll`, `plcmap.dll`, MSIs, installers | HEIDENHAIN | proprietary |
| `VirtualBox-7.1.4-…-Win.exe` | Oracle | VirtualBox base = GPLv2 (binary), but **redistribution of Oracle's installer is governed by Oracle's terms**; just don't ship it — users install upstream VirtualBox themselves |
| The PDF manuals | HEIDENHAIN | copyrighted documentation |

→ **None of these may go into a public Git repo.** They are excluded by `.gitignore`
(`34059518SP4/`, `work/`, plus image/installer extensions).

## What we *can* do and publish

Our **documentation, notes, measurements, diagrams, and any clean‑room code** we write are our
own copyrightable work and are fine to publish. Key legal footing (EU, where the user is based):

- **Directive 2009/24/EC (Computer Programs Directive):**
  - **Art. 5(3)** — a lawful user may **observe, study and test** the program to determine the
    ideas/principles behind it, *without* the rightholder's authorization. Contract terms
    forbidding this are **null and void** (Art. 8).
  - **Art. 6** — **decompilation is permitted** specifically to achieve **interoperability** of
    an independently created program, under conditions (info not otherwise available, limited to
    the parts necessary, not used for other purposes, not to create a substantially similar
    program). Documenting the host↔guest interfaces (shared folders, guest properties, the
    `heuinput` FIFO, port 19035, the JHIO block) to build an **interoperable host** is the
    textbook use case.
- These rights are **non‑overridable by EULA** for those purposes — so an EULA "no reverse
  engineering" clause does not bar interoperability analysis in the EU. (Outside the EU the
  analysis differs; US law leans on fair use + interoperability precedents like *Sega v.
  Accolade* and the DMCA §1201(f) interoperability exception.)

### Guardrails we follow

1. **Possess legitimately.** Only analyze a copy you downloaded/own; honor demo‑mode limits;
   don't crack the dongle/SIK licensing. (Running **demo mode**, which needs no dongle, side‑steps
   the licensing question entirely.)
2. **No redistribution of their bits.** No OVA/VMDK/extpack/binaries/manuals/installer in the
   repo or anywhere public. Reference them by name and document behaviour instead.
3. **Interoperability, not cloning.** The aim is a host that *talks to* HEIDENHAIN's VM, not a
   re‑implementation of the control software. If we write a JHIO host service, it's a clean‑room
   interoperability shim, documented as such.
4. **Don't quote the manuals at length.** Cite facts (ports, IDs, steps) — fair‑use‑sized — not
   wholesale copied pages.
5. **Trademarks:** "HEIDENHAIN", "TNC", "HeROS", "VirtualBox" are trademarks of their owners;
   used here only descriptively (nominative fair use). This project is **not affiliated with or
   endorsed by HEIDENHAIN or Oracle.**

## Recommended repo hygiene

- Keep the `.gitignore` excluding `34059518SP4/`, `work/`, and image/installer file types.
- Before any push, run a check (see [10 — Methodology](10-methodology.md)) that no proprietary
  blobs are staged.
- A short disclaimer in the README (present) + this file.
- If you ever want others to reproduce, tell them to obtain the package from HEIDENHAIN
  themselves — never host it.

## Bottom line

Public repo of **documentation + clean‑room interop code = fine**. Public repo containing
**HEIDENHAIN/Oracle binaries or the OVA/manuals = not fine** (copyright infringement,
DMCA‑takedown‑able, and the JHIO license forbids it explicitly). This project is built to stay
firmly on the right side of that line.
