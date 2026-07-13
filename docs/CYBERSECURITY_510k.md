# Cybersecurity Documentation for Premarket Submission (FDA 510(k))

**Device / Subsystem:** open-LIFU Console Secure Bootloader & Secure Firmware Update (STM32F072 MCU subsystem)
**Manufacturer:** Openwater
**Document type:** Premarket cybersecurity documentation
**Status:** DRAFT — engineering input for 510(k) submission
**Version:** 0.1
**Date:** 2026-07-13

> **Scope & disclaimer.** This document provides the **engineering/technical** cybersecurity
> content that supports a 510(k) premarket submission for the device's embedded
> firmware-security subsystem (secure boot and secure firmware update). It is organized
> per the FDA guidance *Cybersecurity in Medical Devices: Quality System Considerations
> and Content of Premarket Submissions* (Sept 2023) and references **ANSI/AAMI SW96:2023**,
> **AAMI TIR57**, **IEC 81001-5-1**, and **NIST SP 800-30/-218**. It is **not** regulatory or
> legal advice. Device-level regulatory fields (intended use, classification, predicate,
> clinical claims) and the overall **security risk management file** must be completed and
> approved by Openwater Regulatory Affairs and Quality. Placeholders are marked `[[TODO: ...]]`.

> **Platform note.** This subsystem is the STM32F072 (Arm Cortex-M0) sibling of the openMOTION
> Console secure bootloader (STM32H743). It uses the **same Root-of-Trust architecture and the
> same signed-image format**, but the STM32F072 is a smaller, lower-cost part that **lacks the
> STM32H7 hardware security IP (MPU, PCROP, Firewall, secure-user/HDP memory)**. Where the H743
> relies on those IPs, this subsystem relies on the available F0 protections (Flash WRP, RDP,
> IWDG) plus software controls (Secure-Engine call-gate checks, SE-RAM key wipe, DFU-window
> clamping). These platform differences are called out throughout (§2.1, §5, §6, §12) and are
> material to the residual-risk assessment.

---

## Table of contents

1. [Introduction and scope](#1-introduction-and-scope)
2. [Device description (security-relevant)](#2-device-description-security-relevant)
3. [Security risk management](#3-security-risk-management)
4. [Threat model](#4-threat-model)
5. [Security architecture](#5-security-architecture)
6. [Security controls](#6-security-controls)
7. [Software bill of materials (SBOM)](#7-software-bill-of-materials-sbom)
8. [Cybersecurity testing and verification](#8-cybersecurity-testing-and-verification)
9. [Vulnerability and postmarket management](#9-vulnerability-and-postmarket-management)
10. [Security labeling](#10-security-labeling)
11. [Unresolved anomalies](#11-unresolved-anomalies)
12. [Production hardening requirements](#12-production-hardening-requirements-critical)
13. [Appendix A — Security requirements traceability matrix](#appendix-a--security-requirements-traceability-matrix)
14. [Appendix B — Cryptographic details](#appendix-b--cryptographic-details)
15. [Appendix C — Glossary](#appendix-c--glossary)

---

## 1. Introduction and scope

### 1.1 Purpose
This document describes the cybersecurity design, risk management, controls, and verification
for the open-LIFU Console **secure bootloader** and **secure firmware update** subsystem. This
subsystem establishes a firmware **Root of Trust** that ensures only firmware authored and signed
by Openwater can execute on the device, and that firmware updates are authenticated and
integrity-checked before execution.

### 1.2 Scope
**In scope:**
- The immutable secure bootloader (SBSFU/SECoreBin) executing on the STM32F072 MCU.
- Cryptographic verification of application firmware (authenticity + integrity).
- The secure firmware update path (USB DFU; signed image installation).
- Flash memory partitioning and access controls (read-only bootloader and config regions).
- Key provisioning and management for firmware signing/verification.

**Out of scope (covered in separate documents):**
- The application firmware's own clinical/functional cybersecurity controls. `[[TODO: reference]]`
- Host/companion software, cloud services, and network interfaces. `[[TODO: reference]]`
- Physical/electrical safety (IEC 60601). `[[TODO: reference]]`

### 1.3 Intended use / device context
`[[TODO: Regulatory — insert device intended use, indications, classification (e.g., Class II),
product code, predicate device(s), and whether the device is networked, multi-patient, or
life-sustaining. These drive the cybersecurity risk tier per FDA guidance. Note: open-LIFU is a
low-intensity focused-ultrasound console; the STM32F072 subsystem is the console power/control
board.]]`

---

## 2. Device description (security-relevant)

### 2.1 Hardware platform
| Item | Detail |
|------|--------|
| MCU | STMicroelectronics STM32F072xB (Arm Cortex-M0, ARMv6-M) |
| Internal flash | 128 KB, single-bank, 64 × 2 KB pages |
| SRAM | 16 KB |
| Security IP | Flash Write Protection (WRP), Readout Protection (RDP), independent watchdog (IWDG). **No MPU, no PCROP, no Firewall, no secure-user (HDP) memory** on this part. |
| External interfaces | USB (DFU bootloader interface; application comms), USART3 debug trace (PC10/PC11), I²C, GPIO |

> **Consequence of the platform.** Because the STM32F072 has no MPU or PCROP, the Secure Engine
> region cannot be *hardware*-isolated from the application at runtime, and the on-device key
> material cannot be made execute-only in flash. The Root of Trust is instead protected by
> (a) placing the bootloader/keys in a DFU-read-only region, (b) **RDP** (production) to block
> debug-port readout, (c) **WRP** (production) to make the bootloader immutable, and (d) a
> software **SE-RAM key wipe** performed before control ever leaves the trusted bootloader. See
> §5.3, §6 (C-7), and §12.

### 2.2 Software components
| Component | Role | Mutability |
|-----------|------|-----------|
| SECoreBin (Secure Engine) | Crypto services + key custody, linked into the bootloader image | Immutable (programmed at manufacture) |
| SBSFU bootloader | Root of Trust; verifies + launches application; secure update | Immutable (programmed at manufacture) |
| Application firmware | Device clinical/functional firmware | Field-updatable (signed) |

> Unlike the two-binary STM32H743 build, the F072 is a **single-image build**: the Secure Engine
> core is linked into the bootloader ELF, with the SE call gate, key block, and SE startup pinned
> at fixed flash addresses. The ST HAL/CMSIS/USB components are baselined on **STM32CubeF0
> V1.11.6**; the SBSFU/SECoreBin Root of Trust is ported from ST X-CUBE-SBSFU; the cryptographic
> primitives come from the ST X-CUBE-CRYPTOLIB V5 (CMOX) Cortex-M0/M0+ library. Exact versions are
> in the SBOM (§7).

### 2.3 Flash partition map
Defined in `Core/Inc/memory_map.h` (single source of truth), mirrored in
`Linker/mapping_sbsfu.ld` and `Linker/mapping_fwimg.ld`:

```
Addr range              Size   Pages   Region                 Access control
------------------------------------------------------------------------------------
0x08000000-0x0800FFFF   64K    0-31    BOOTLOADER (+ SE keys) read-only (immutable)
0x08010000-0x0801EFFF   60K    32-61   APP SLOT 1 (active)    updatable (signed) — DFU writable window
0x0801F000-0x0801F7FF    2K    62      APP RUNTIME CONFIG     read-only via DFU (application-managed)
0x0801F800-0x0801FFFF    2K    63      USER CONFIG            read-only via DFU (preserved)
```

The DFU update interface restricts erase/write to the **active application slot only**
(`0x08010000`–`0x0801EFFF`, the SBSFU `SLOT_ACTIVE_1` extent in `Linker/mapping_fwimg.ld`;
DfuSe descriptor `32*2Ka,30*2Kg,02*2Ka`). The DFU writable window is deliberately clamped to the
slot so that **all DFU-writable flash is covered by secure-boot slot verification** (no writable
region escapes the `VerifyActiveSlot` check). The bootloader pages (0–31), the application
runtime-config page (62), and the user-config page (63) are **not erasable or writable** through
the firmware-update (DFU) interface.

> **Design rule — application persistent data lives OUTSIDE the slot.** The secure boot verifies
> not only the image signature and hash but that the slot contains *nothing beyond* the signed
> image (`SFU_IMG_VerifyActiveSlot`, threat T-7). Therefore any flash the application writes at
> run time (configuration, calibration) **must** reside outside the active slot. The console
> application keeps its runtime config in page 62 (`0x0801F000`), which is why the slot ends at
> `0x0801EFFF`. Placing application-written data inside the slot would cause the *next* boot to
> detect non-blank bytes past the image and fail verification, safely invalidating a genuine
> firmware. This is a verification property working as designed, not a defect (see §8, TC-CONFIG-01).

### 2.4 Cryptographic scheme
Scheme identifier `SECBOOT_ECCDSA_WITH_AES128_CBC_SHA256`, configured for the **NO_LOADER /
direct-flash** flow:
- **Authenticity:** ECDSA P-256 signature over the firmware image header (public key on device).
- **Integrity:** SHA-256 digest of the firmware binary, bound into the signed header.
- **Confidentiality (transport):** **NOT active in this configuration.** The firmware body is
  stored and distributed in **clear** (the active slot holds plaintext; the AES-CBC decrypt path
  is unused). The AES-128 key is embedded for scheme compatibility but is never exercised at boot.
  Firmware-IP confidentiality is therefore **not** a control provided by this subsystem — see the
  threat-model note on T-4 (§4.3) and §6 (C-5).

---

## 3. Security risk management

### 3.1 Process
Security risk management is integrated with the product risk management process (ISO 14971)
and performed per **AAMI TIR57 / ANSI-AAMI SW96** using **NIST SP 800-30** methodology.
Security risks that can lead to patient harm are escalated to the safety risk file.
`[[TODO: Quality — reference the controlled Security Risk Management Plan, file ID, and approvals.]]`

### 3.2 Security objectives
| ID | Objective |
|----|-----------|
| SO-1 | Only firmware authentic to Openwater executes on the device (authenticity). |
| SO-2 | Firmware cannot be silently modified without detection (integrity). |
| SO-4 | The Root of Trust (bootloader, keys) is immutable and tamper-resistant. |
| SO-5 | A failed/forged update cannot brick the device or run unauthenticated code (resilience). |
| SO-6 | Field updates are controlled, authenticated, and auditable (updateability). |

> SO-3 (firmware-IP confidentiality) from the STM32H743 subsystem is **not claimed** for this
> subsystem, because the F072 configuration distributes/stores firmware in clear (§2.4). If IP
> confidentiality becomes a requirement, the encrypted-install flow and on-device AES key
> protection (which requires PCROP — unavailable on F0) would need to be re-evaluated.

### 3.3 Risk acceptance
`[[TODO: define residual-risk acceptance criteria and link to the security risk assessment
worksheet. Each threat in §4 maps to a control in §6 and a verification in §8 — see Appendix A.
The residual-risk assessment MUST explicitly account for the absence of MPU/PCROP on this part
(§2.1, §12).]]`

---

## 4. Threat model

Methodology: **STRIDE** over the data-flow and trust boundaries of the boot/update subsystem.

### 4.1 Assets
- A1: Application firmware authenticity/integrity (and patient safety that depends on it).
- A3: Cryptographic keys (signing private key off-device; public + symmetric keys on-device).
- A4: Root of Trust (immutable bootloader / SECoreBin).
- A5: Device availability (must boot or safely enter recovery).

### 4.2 Trust boundaries & attack surfaces
| # | Surface | Description |
|---|---------|-------------|
| AS-1 | USB DFU interface | Firmware install endpoint exposed when no valid firmware present (or on request). |
| AS-2 | Physical flash / debug (SWD) | Direct read/modify of flash or RAM via debugger. |
| AS-3 | Firmware distribution channel | Path by which signed images reach the device. |
| AS-4 | Application ↔ bootloader handoff | Control transfer and shared state at launch (RTC backup registers, SRAM vector table). |
| AS-5 | Signing infrastructure (off-device) | Where the ECDSA private key is held/used. |

### 4.3 STRIDE threats and mitigations
| ID | STRIDE | Threat | Mitigation (control) | Ref |
|----|--------|--------|----------------------|-----|
| T-1 | Tampering | Attacker replaces/patches application firmware in flash. | Secure boot verifies ECDSA header signature + SHA-256 FW tag every boot; rejects on mismatch. | C-1, C-2 |
| T-2 | Spoofing | Attacker installs forged firmware via DFU. | DFU image must carry a valid ECDSA signature over the header; unauthenticated images are not executed. | C-1, C-4 |
| T-3 | Tampering | Attacker modifies the bootloader or keys. | Bootloader + SE keys in DFU-read-only region (pages 0–31); (production) Flash WRP makes it immutable and RDP blocks readout. **No PCROP/MPU on this part** — see §12. | C-3, C-7 |
| T-4 | Information disclosure | Firmware IP extracted in transit or from flash. | **Not mitigated** in the current clear-image configuration (§2.4). Firmware is distributed/stored in clear; treat firmware IP as non-confidential for this subsystem, or add an encrypted-install flow (requires re-evaluation on F0). | — |
| T-5 | Information disclosure | Keys/firmware read out via debug port. | (Production) **RDP Level ≥ 1** blocks SWD flash readout; RDP Level 2 permanently disables debug. Note: without PCROP the AES key and public key are readable from flash **until RDP is applied**. | C-7 |
| T-6 | Denial of service | Corrupt/partial update bricks the device. | Update is verified before execution; invalid slot → safe recovery (DFU re-entry), never executes bad code. Boot-fail counter forces DFU after repeated failed launches. | C-2, C-6 |
| T-7 | Tampering | "Additional code beyond firmware" hidden in slot. | Bootloader verifies the unused slot region is empty (`VerifyActiveSlot`) before launch; application persistent data is kept outside the slot by design (§2.3). | C-2 |
| T-8 | Elevation of privilege | Application calls protected Secure Engine services illegitimately. | SE call-gate validates that the caller's return address is within the trusted SE-interface region (`IS_CALLER_SE_IF`, resets the MCU otherwise). **No MPU privilege isolation on this part**; the app runs privileged, so the durable protection is the **SE-RAM key wipe** performed before launch (§5.3). | C-7 |
| T-9 | Tampering | Rollback to a known-vulnerable signed firmware. | Signed-header firmware version (`FwVersion`); **DFU-time downgrade rejection** compares the incoming image's version to the installed one and invalidates an obvious downgrade before it can boot. **No persistent monotonic floor** is implemented on this part (no spare flash page on 128 KB) — this is a residual-risk difference from the STM32H743 subsystem (§5.6, §11 AN-2). | C-8 |
| T-10 | Spoofing | Compromise of the off-device signing key. | HSM/controlled key custody, least-privilege signing, key rotation procedure. Signing private key never on device. | C-9 |

### 4.4 Multi-patient / safety impact
`[[TODO: Risk — assess whether a single exploited unit can affect multiple patients or whether
a vulnerability is reproducible fleet-wide. The Root of Trust limits fleet compromise to entities
possessing the signing key.]]`

---

## 5. Security architecture

### 5.1 Architecture overview (Root of Trust)
On every reset the **immutable secure bootloader** executes first (Root of Trust). It initializes
the Secure Engine, then:
1. Detects an application image in the active slot.
2. Verifies the image **header signature** (ECDSA P-256) using the embedded public key.
3. Verifies the firmware **integrity** (SHA-256) against the value bound in the signed header.
4. Verifies no extraneous code exists beyond the firmware in the slot.
5. Only if all checks pass, transfers control to the application; otherwise it does not execute
   the image and enters the controlled update (DFU) path.

```
        +----------------+   verify (ECDSA + SHA-256)    +------------------+
 RESET -> Secure Boot     +------------------------------->  Application FW  |
        | (Root of Trust) |   pass -> launch              |  (active slot)   |
        +-------+---------+                                +------------------+
                | fail / no valid FW
                v
        +----------------+   authenticated install (signed image only)
        | USB DFU update  |<------------------------------  Host tool (dfu-util / signed installer)
        +----------------+
```

### 5.2 Security architecture views (FDA-required)
- **Global system view:** §2.1–2.3 (hardware, software, partitions).
- **Updateability / patchability view:** §5.4 and §9 (how firmware is updated and patched).
- **Multi-patient harm view:** §4.4 `[[TODO]]`.
- **Security use case views:** boot-time verification (§5.1), field update (§5.4), key provisioning (§6 C-9).

### 5.3 Secure boot (detailed)
- Bootloader and Secure Engine reside in flash **pages 0–31 (0x08000000–0x0800FFFF)**, marked
  read-only over the update interface and (in production) write-protected by hardware WRP.
- The Secure Engine exposes cryptographic services through a **call-gate**. The call gate resides
  at a fixed address and validates that its caller's return address lies within the trusted
  SE-interface region; a caller outside that region triggers `NVIC_SystemReset()`. Because the
  F072 has no MPU, this is a **software** integrity check, not hardware isolation.
- **SE-RAM key wipe.** Before control leaves the bootloader (both the app-launch path and the
  DFU-entry path), the Secure Engine RAM region (`0x20000000`–`0x20000FFF`, which holds the
  decrypted key material, the CMOX ECDSA working buffer, and the SHA-256/hash state) is
  **zeroized**. This limits the window in which key-derived material is resident once an
  (unprivileged-isolation-less) application is running.
- **Cortex-M0 launch.** The M0 has no VTOR: the bootloader copies the application vector table to
  the base of SRAM (`0x20000000`) and remaps SRAM to `0x00000000` (SYSCFG) before the jump, then
  restores the RCC to its reset state so the application's clock init starts clean. Interrupts are
  masked only across the transition and re-enabled so the application starts as it would from reset.
- Verification is **fail-closed**: any signature, hash, or slot-layout check failure prevents
  execution of the candidate image and routes to the DFU recovery path.

### 5.4 Secure firmware update (detailed)
- Update images are produced by the controlled signing tool (`py-tools/sign_firmware.py`): a
  320-byte (0x140) header — magic `SFU1`, protocol/firmware version, sizes, SHA-256 FW tag, IV,
  **ECDSA-P256 signature over the first 128 bytes of the header**, image-state marker, and
  previous-header fingerprint — followed by `0xFF` padding to offset `0x400` and the firmware body
  (in clear) at the slot execution offset.
- The device exposes a **USB DFU** interface (VID:PID `0483:DF11`) when no valid firmware is
  present, or upon an authenticated in-application request (RTC backup-register `'DFU!'` magic +
  reset — see §9). The DFU media layer restricts writes/erases to the active slot; targets in the
  bootloader region, the app-config page, and the user-config page are rejected.
- The DFU **read/UPLOAD** path is also bounds-checked to the active slot (`MEM_If_Read_FS` returns
  a STALL for any address outside the slot window), preventing readout of the bootloader/key region
  or RAM through the update interface. A virtual UPLOAD address returns only the version string.
- After download the device resets and re-runs full verification before executing the new image.

### 5.5 Cryptographic design
See Appendix B. The ECDSA **public** key and the (unused-at-boot) AES-128 key are embedded in the
Secure Engine key block at manufacture; the **ECDSA private signing key never resides on the
device**. On-device verification uses the ST CMOX software library (ECDSA P-256 verify with a
fault-injection double-check, and SHA-256). A boot-time verify completes in well under one second
at 48 MHz.

### 5.6 Anti-rollback (downgrade protection) — reduced scope on this platform
- **DFU-time check (implemented).** During download the bootloader reads the incoming image's
  signed `FwVersion` from its freshly-written header and compares it against the version installed
  at DFU entry. An obvious downgrade is **invalidated** (its header is erased) so it cannot boot.
- **Persistent monotonic floor (NOT implemented on F072).** The STM32H743 subsystem stores a
  monotonic version floor in a dedicated flash sector outside the DFU window. On the 128 KB
  STM32F072 there is **no spare page** to hold such a floor without shrinking the application slot
  below the current firmware size, so the persistent floor is **not** present here. Consequently a
  downgrade to a validly-signed older image is only blocked at DFU-install time, not re-checked at
  every boot. This is tracked as a residual-risk difference (§11 AN-2). If a persistent floor
  becomes a requirement, options are: reserve a page by shrinking the slot, or co-locate the floor
  in the app/user-config page. `[[TODO: Risk — accept or schedule.]]`

---

## 6. Security controls

| ID | Control | Implements | Verification |
|----|---------|-----------|--------------|
| C-1 | ECDSA P-256 authentication of firmware header (CMOX, fault-checked) | SO-1, T-1/T-2 | §8 TC-AUTH-* |
| C-2 | SHA-256 integrity + empty-slot check (`VerifyActiveSlot`) | SO-2, T-1/T-6/T-7 | §8 TC-INTEG-*, TC-CONFIG-01 |
| C-3 | Immutable bootloader/SE in DFU-read-only region (pages 0–31) | SO-4, T-3 | §8 TC-IMMUT-* |
| C-4 | DFU restricted to app slot; signed-only install; bounded UPLOAD | SO-6, T-2 | §8 TC-DFU-* |
| C-5 | Transport confidentiality (AES-128-CBC) | SO-3 | **NOT active** — clear-image flow (§2.4) |
| C-6 | Fail-closed verification; safe recovery on invalid FW; boot-fail counter → DFU | SO-5, T-6 | §8 TC-FAILCLOSED-* |
| C-7 | Hardware/software protections: (prod) RDP + WRP; SE call-gate caller check; SE-RAM key wipe | SO-4, T-3/T-5/T-8 | §8 TC-HWPROT-* / §12 |
| C-8 | Signed `FwVersion` + **DFU-time** downgrade rejection (no persistent floor, §5.6) | SO-5, T-9 | §8 TC-ROLLBACK-* |
| C-9 | Off-device key custody (HSM), provisioning, rotation | A3, T-10 | §8 TC-KEY-* `[[TODO procedure]]` |
| C-10 | Protected config pages (no DFU erase/write): app config (62), user config (63) | data integrity | §8 TC-USERCFG-* |

---

## 7. Software bill of materials (SBOM)

The SBOM must be delivered in a machine-readable format (**SPDX** or **CycloneDX**) per FDA guidance.
The table below is the human-readable summary; `[[TODO: generate the machine-readable SBOM in CI and
attach it; include exact versions, suppliers, licenses, and known-vulnerability status (e.g., via NVD).]]`

| Component | Supplier | Version | Type | License | Notes |
|-----------|----------|---------|------|---------|-------|
| STM32 Secure Engine (SBSFU/SECoreBin) | STMicroelectronics (X-CUBE-SBSFU) | 2.8.0 (ported) | Middleware | ST SLA | Root of Trust; single-image build |
| STM32 Cryptographic Library (CMOX) | STMicroelectronics (X-CUBE-CRYPTOLIB) | V5.0.0, `libSTM32Cryptographic_CM0_CM0PLUS.a` | Crypto lib | ST SLA | ECDSA P-256 verify + SHA-256 (Cortex-M0 build) |
| STM32F0 HAL / CMSIS | STMicroelectronics | STM32CubeF0 V1.11.6 | HAL/driver | BSD-3 / Apache-2.0 | Sourced from STM32CubeF0 firmware package V1.11.6 |
| STM32 USB Device Library (DFU) | STMicroelectronics | STM32CubeF0 V1.11.6 | Middleware | ST SLA | Update interface |
| arm-none-eabi-gcc | Arm | 15.2.rel1 | Toolchain | GPL (toolchain) | Build only |
| Python `cryptography` | PyCA | ≥ 41.0 | Build/sign tool | Apache-2.0/BSD | Off-device signing + key generation |
| Python `pyusb` + libusb | PyUSB / libusb | 1.x / 1.0 | Build/host tool | BSD / LGPL-2.1 | Off-device flasher (`flash_firmware.py`) |

> Off-device build/sign/flash tools are listed because they form part of the secure-update **supply
> chain** even though they do not run on the device.

---

## 8. Cybersecurity testing and verification

FDA expects evidence across four areas: security-requirements testing, threat-mitigation testing,
vulnerability testing, and penetration testing.

### 8.1 Security requirements / threat-mitigation testing

> **Bench status (2026-07-10/11, STM32F072 target, `next` branch, CMOX build).** The full secure-
> boot chain was exercised end-to-end on hardware (ST-Link + USART3 trace on COM5 + USB DFU): flash
> bootloader → empty slot enters DFU → `dfu-util`/Python install of the signed image → reset →
> `VERIFY USER FW SIGNATURE` (ECDSA + SHA-256 both pass) → `EXECUTE USER FIRMWARE` → application
> runs. Boot reliability was **16/16** clean app-boots across two reset loops, plus **6/6** with the
> application's runtime config already written to page 62 — the scenario that exercises the
> empty-slot check (TC-CONFIG-01). Rows marked `[[TODO]]` are implemented in code but not yet
> re-captured as formal evidence on the F072 target, or require the production hardware-protection
> build (§12) / process evidence.

| Test ID | Objective | Method | Result |
|---------|-----------|--------|--------|
| TC-AUTH-01 | Valid signed image boots | Sign with the provisioned console key, install via DFU, observe execution | **Pass** (STM32F072): `EXECUTE USER FIRMWARE`; app banner on COM5; 16/16 + 6/6 reboots |
| TC-AUTH-02 | Image with tampered body is rejected | Flip bytes after signing; attempt boot | `[[TODO: capture on F072]]` — offline round-trip confirms FwTag binds the exact body; the SHA-256 check (TC-INTEG) is the on-device mechanism |
| TC-AUTH-03 | Image signed with wrong key is rejected | Sign with a non-provisioned ECDSA P-256 key; install via DFU | `[[TODO: capture on F072]]` — public/AES key binding verified (embedded key == provisioned `bl-keys/console`); expected reject at `VERIFY USER FW SIGNATURE` |
| TC-INTEG-01 | SHA-256 mismatch rejected | Valid signed header, flip one body byte without re-signing; install via DFU | `[[TODO: capture on F072]]` — FwTag = SHA-256(clear body); mechanism exercised by TC-CONFIG-01 tail check |
| TC-INTEG-02 / TC-CONFIG-01 | Extra/again-written content beyond firmware rejected; app config outside slot is safe | (a) App writes its runtime config to page 62 (`0x0801F000`, outside slot); reboot repeatedly. (b) Historical: config page inside the slot caused `VerifyActiveSlot` to reject after first save. | **Pass** (STM32F072): with config **outside** the slot, 6/6 reboots boot the app; with config **inside** the slot, `VerifyActiveSlot` correctly rejected the slot (`EXECUTE` withheld, enters DFU) — demonstrating the empty-slot integrity control works. Fix: slot ends at `0x0801EFFF`, config at `0x0801F000`. |
| TC-DFU-01 | DFU install of signed image succeeds | `dfu-util -a 0 -s 0x08010000 -D signed.bin` and pure-Python `flash_firmware.py` | **Pass** (both paths; Python flasher does a full-slot erase) |
| TC-DFU-02 | DFU write to bootloader region rejected | `dfu-util` download targeting `0x08000000` | `[[TODO: capture on F072]]` — DfuSe descriptor marks pages 0–31 read-only (`32*2Ka`); `MEM_If_Write_FS` rejects `addr < 0x08010000` |
| TC-DFU-03 | DFU write/read outside the active slot rejected | `dfu-util` targeting `0x0801F000` (config) and UPLOAD from `0x08000000` | `[[TODO: capture on F072]]` — write window clamped to `[0x08010000, 0x0801F000)`; `MEM_If_Read_FS` STALLs out-of-window UPLOAD |
| TC-USERCFG-01 | DFU write/erase of config pages rejected | Target `0x0801F000` / `0x0801F800` | `[[TODO: capture on F072]]` — outside DFU writable window |
| TC-FAILCLOSED-01 | No/invalid FW → safe recovery, no code exec | Empty (erased) slot; reset | **Pass** (STM32F072): enters USB DFU; no unauthenticated execution |
| TC-IMMUT-01 | Bootloader not modifiable via update path | Hash bootloader region before/after a DFU write attempt to `0x08000000` | `[[TODO: capture on F072]]` — write rejected by descriptor + bounds check; (prod) enforced by WRP |
| TC-HWPROT-01 | RDP/WRP effective | Read/modify attempts via SWD on a production-provisioned unit | `[[TODO: production build — §12]]` |
| TC-ROLLBACK-01 | DFU-time downgrade rejected | Install vN, then attempt to install v(<N) via DFU | `[[TODO: capture on F072]]` — DFU-time version compare + invalidation implemented; **no persistent boot-time floor** on this part (§5.6, AN-2) |
| TC-KEY-01 | Provisioned key matches build | Confirm embedded public key == `bl-keys/console` public key; AES secret == `bl-keys/console` AES | **Pass** (offline): embedded pub-key words 16/16 match; CI-regenerated `se_key.s` byte-identical to local build; private/public pair consistent |

### 8.2 Vulnerability testing
`[[TODO: known-vulnerability scan of SBOM components against NVD/ICS-CERT; static analysis
(the current build is warning-clean apart from the intentional ST dev-mode
"SFU_*_PROTECT_DISABLED" reminders); fuzzing of the DFU/header parser.]]`

### 8.3 Penetration testing
`[[TODO: independent penetration test of the boot/update subsystem — fault injection, debug-port
attacks, signature-bypass attempts, update-path abuse. Provide report and remediation. Note: on a
part without PCROP/MPU, debug-port and key-readout attacks are gated primarily by RDP — test with
production RDP applied.]]`

---

## 9. Vulnerability and postmarket management

`[[TODO: Quality/Security — reference the controlled Cybersecurity Management Plan. Include:]]`
- **Monitoring:** subscribe to ST PSIRT, NVD, and component advisories for SBOM items (notably the
  CMOX crypto library and STM32CubeF0).
- **Coordinated disclosure:** intake channel and SLA for externally reported vulnerabilities.
- **Patch/update mechanism:** the secure firmware update path (§5.4) is the supported remediation
  channel; signed updates can be deployed in the field via DFU.
- **Update authenticity in the field:** in-application DFU re-entry (RTC `'DFU!'` magic + reset)
  must itself be access-controlled so only authorized operators can place the device into update
  mode. `[[TODO: define trigger & authorization — e.g., authenticated host command.]]`
- **Patch cadence / end-of-support:** `[[TODO]]`.

---

## 10. Security labeling

Per FDA labeling expectations, provide to users/operators: `[[TODO: Regulatory to finalize]]`
- A description of the device's cybersecurity controls (secure boot, signed updates).
- Instructions for performing secure firmware updates and verifying success.
- A statement that only Openwater-signed firmware will run on the device.
- Guidance on physical security of the device and its update/host environment.
- A point of contact for reporting suspected vulnerabilities.
- SBOM availability to operators on request.

---

## 11. Unresolved anomalies

`[[TODO: link each item to the controlled defect system and complete the risk assessment.]]`

| ID | Anomaly | Risk assessment | Disposition |
|----|---------|-----------------|-------------|
| AN-1 | Application runtime-config storage originally overlapped the SBSFU active slot (config page inside `SLOT_ACTIVE_1`). After the application saved its config, the next boot's `VerifyActiveSlot` detected non-blank data beyond the image and the bootloader invalidated a genuine firmware — presenting as "boots once, then continuously reboots / enters DFU." | **Low–Medium** (never a verification bypass — verification correctly rejected non-conforming slot content; the impact was availability/recoverability of a genuine image). | **RESOLVED.** The active slot was shrunk to `0x08010000`–`0x0801EFFF` (60 KB) and the application config page moved to `0x0801F000` (page 62), outside the slot and outside the DFU window. Verified: 6/6 reboots with config saved (TC-CONFIG-01). |
| AN-2 | No persistent boot-time anti-rollback floor on this platform (§5.6). A validly-signed older firmware is blocked at DFU-install time but not re-checked at every boot; an attacker with SWD write access to the slot (which also requires defeating RDP in production) could install an older signed image. | **Low** given RDP-gated debug access in production and DFU-time rejection; higher if debug remains open. | **OPEN / accepted-or-scheduled** `[[TODO: Risk decision]]`. Difference from the STM32H743 subsystem, driven by 128 KB flash with no spare page. |

---

## 12. Production hardening requirements (CRITICAL)

> The development configuration disables the STM32 hardware security features for debuggability
> (`SECBOOT_DISABLE_SECURITY_IPS`), which the build surfaces as intentional `#warning
> "SFU_*_PROTECT_DISABLED"` reminders. **A device shipped for clinical use MUST be built and
> provisioned with these protections ENABLED.** This is a release gate.

| Protection | Dev state | Production requirement (STM32F072) |
|------------|-----------|-----------------------------------|
| RDP (readout protection) | Off | **RDP Level ≥ 1** (blocks SWD flash readout). RDP Level 2 permanently disables debug — irreversible; use per risk assessment. Implemented in `sfu_low_level_security.c` (`SFU_RDP_PROTECT_ENABLE`), compiled out in dev. |
| WRP (write protection) | Off | **WRP on the bootloader + SE key pages** (immutability). Implemented (`SFU_WRP_PROTECT_ENABLE`), compiled out in dev. |
| IWDG watchdog | Off (armed from main only) | **Enabled** per risk assessment. |
| DAP / debug access | Open | Closed via **RDP Level 2** (the F072 has no separate DAP-disable option byte). |
| PCROP (code readout protection) | **Not available on F072** | N/A — the on-device AES/public key cannot be made execute-only; confidentiality of on-device keys relies on RDP. Document as a platform limitation. |
| MPU / SE isolation | **Not available on F072** | N/A — no hardware privilege isolation between app and SE. Compensating control: SE call-gate caller check + SE-RAM key wipe (§5.3). Document as a platform limitation. |

`[[TODO: Manufacturing — document the Option Byte provisioning step (RDP/WRP) and verification at
production test, and confirm keys are unique-per-product or per-product-family per the security
risk assessment. Confirm the residual-risk file explicitly accepts the absence of PCROP/MPU.]]`

---

## Appendix A — Security requirements traceability matrix

| Threat | Objective | Control | Test |
|--------|-----------|---------|------|
| T-1 | SO-1, SO-2 | C-1, C-2 | TC-AUTH-02, TC-INTEG-01 |
| T-2 | SO-1, SO-6 | C-1, C-4 | TC-AUTH-03, TC-DFU-01 |
| T-3 | SO-4 | C-3, C-7 | TC-IMMUT-01, TC-HWPROT-01 |
| T-4 | — (not claimed) | C-5 (inactive) | §2.4 note |
| T-5 | SO-4 | C-7 | TC-HWPROT-01 |
| T-6 | SO-5 | C-2, C-6 | TC-FAILCLOSED-01 |
| T-7 | SO-2 | C-2 | TC-INTEG-02 / TC-CONFIG-01 |
| T-8 | SO-4 | C-7 | TC-HWPROT-01 |
| T-9 | SO-5 | C-8 | TC-ROLLBACK-01 |
| T-10 | A3 | C-9 | TC-KEY-01 |

---

## Appendix B — Cryptographic details

| Function | Algorithm | Parameters | Role |
|----------|-----------|-----------|------|
| Firmware authentication | ECDSA | curve P-256 (secp256r1), SHA-256, fault-injection double-check | Verifies the firmware header signature (authenticity) |
| Firmware integrity | SHA-256 | 256-bit digest | Digest of the firmware body, bound in the signed header (FwTag) |
| Firmware confidentiality (transport) | AES-CBC | AES-128 | **Not active** in this configuration (clear-image flow, §2.4) |

On-device implementation: ST X-CUBE-CRYPTOLIB V5 (**CMOX**), `libSTM32Cryptographic_CM0_CM0PLUS.a`
(`cmox_ecdsa_verify` with `CMOX_ECC_SECP256R1_LOWMEM_VERIFY`, and `cmox_sha256_*`). Acceptance
requires **both** the return value and the fault-check output to equal `CMOX_ECC_AUTH_SUCCESS`.

**Key inventory**
| Key | Type | Location | Protection |
|-----|------|----------|-----------|
| Firmware signing key | ECDSA P-256 private | **Off-device** (signing infrastructure / CI secret in the application repo) | HSM/controlled custody `[[TODO]]` |
| Firmware verification key | ECDSA P-256 public | On-device (SE key block, `0x08000200`) | Immutable; (prod) RDP/WRP. No PCROP on F072. |
| Firmware symmetric key | AES-128 | On-device (SE key block) | (prod) RDP/WRP. **Unused at boot** (§2.4). No PCROP on F072. |

**Key provisioning & CI.** The on-device key block (`SECoreBin/Startup/se_key.s`, Cortex-M0 Thumb-1
encoding) is **not committed** (it embeds the AES key). It is regenerated at build time from the
committed **public** key (`py-tools/keys/ecdsa_public.pem`) plus the `SECOREBIN_AES_KEY` CI secret
(base64 of the 16-byte AES key). Product keys are held in `bl-keys/console` (private key + AES,
never committed). The signing private key is provisioned only to the application repository's
signing pipeline.

**Key management requirements:** generation in a controlled environment; private key never exported
in clear; documented rotation and revocation; per-product or per-family scoping per risk
assessment; re-signing of firmware on key rotation. `[[TODO: reference the Key Management Procedure SOP.]]`

---

## Appendix C — Glossary

| Term | Definition |
|------|------------|
| Root of Trust | Immutable code (secure boot) trusted implicitly, anchoring all subsequent trust. |
| SBSFU | Secure Boot and Secure Firmware Update (ST framework). |
| SECoreBin | Secure Engine binary/module holding crypto + keys; linked into the bootloader on this single-image build. |
| CMOX | ST X-CUBE-CRYPTOLIB V5 cryptographic library (provides ECDSA/SHA-256 on Cortex-M0). |
| DFU | Device Firmware Upgrade (USB class) used as the update interface. |
| SBOM | Software Bill of Materials. |
| RDP / WRP / PCROP | STM32 flash readout / write / code-readout protections (Option Bytes). PCROP is **not** available on STM32F072. |
| MPU | Memory Protection Unit — **not** present on the Cortex-M0 STM32F072. |
| VerifyActiveSlot | Bootloader check that no code exists in the active slot beyond the signed firmware image. |
| Fail-closed | On verification failure, the system denies execution rather than proceeding. |

---

*End of document. Sections marked `[[TODO]]` require completion/approval by Regulatory Affairs,
Quality, and Security before submission.*
