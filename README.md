# Openwater Open-LIFU Console Secure Bootloader

STM32F072 secure bootloader (SBSFU) for the Console Power PCB. This is a port
of the proven Openwater open-motion-console-bl (STM32H743) secure boot chain,
built on ST's X-CUBE-SBSFU Secure Boot / Secure Firmware Update architecture,
with cryptography provided by the ST X-CUBE-CRYPTOLIB V5 (CMOX) Cortex-M0/M0+
library. A device-specific cybersecurity summary for FDA 510(k) submission is
in [`docs/CYBERSECURITY_510k.md`](docs/CYBERSECURITY_510k.md).

## Security Model

Every boot performs full verification of the application image in the active
slot before launching it:

- **Header authentication**: ECDSA P-256 signature (raw `r||s`) over the first
  128 bytes of the SFU1 image header, verified against the product public key
  embedded in the bootloader (CMOX `cmox_ecdsa_verify`, secp256r1, with a
  fault-injection double check).
- **Firmware integrity**: SHA-256 firmware tag (`FwTag`) over the full
  application body, compared against the authenticated header field.
- **Slot hygiene**: the slot beyond the image must be erased (no trailing
  code).

The image body is stored in CLEAR in the active slot (direct-flash / NO_LOADER
model): USB DFU writes the signed image straight into the slot and the next
boot verifies it. There is no encrypted install path.

Crypto primitives come from the ST X-CUBE-CRYPTOLIB V5 (CMOX) Cortex-M0/M0+
software library (`Middlewares/ST/STM32_Cryptographic/lib/libSTM32Cryptographic_CM0_CM0PLUS.a`):
SHA-256 (`cmox_sha256_*`) and ECDSA P-256 verify (`cmox_ecdsa_verify`,
`CMOX_ECC_SECP256R1_LOWMEM_VERIFY`). A full verify (ECDSA header + SHA-256 over
the 46 KB image) takes well under a second at 48 MHz. The verify is wrapped in
`SECoreBin/Src/se_crypto_bootloader.c` and uses a fault-injection double check
(both the return value and the `fault_check` out-param must equal
`CMOX_ECC_AUTH_SUCCESS`).

## Memory Map (128K flash, 2K pages)

| Region        | Range                     | Size | DFU access |
|---------------|---------------------------|------|------------|
| Bootloader    | `0x08000000`–`0x0800FFFF` | 64 K | read-only  |
| Active slot   | `0x08010000`–`0x0801EFFF` | 60 K | erase/write/read |
| App config    | `0x0801F000`–`0x0801F7FF` |  2 K | none (app-owned) |
| User config   | `0x0801F800`–`0x0801FFFF` |  2 K | none (preserved) |

Signed image layout inside the slot (`py-tools/sign_firmware.py`):

- `0x08010000`: SFU1 header (0x140 bytes: magic, version, size, FwTag,
  ECDSA signature) padded with `0xFF` to `0x400`
- `0x08010400`: application image — **the app links and executes here**

**Important — application persistent data must live OUTSIDE the slot.** The
secure bootloader verifies not only the signature but that the slot contains
*nothing beyond* the signed image (`SFU_IMG_VerifyActiveSlot`). If the
application writes runtime data (config, calibration, logs) into a flash page
inside the slot, the next boot sees non-blank bytes past the image, the verify
fails, and the bootloader **invalidates the firmware** — a good image gets
bricked after its first save. The console app keeps its runtime config in the
dedicated page `0x0801F000` (see `openlifu-console-fw/Core/Src/lifu_config.c`),
which is why the slot ends at `0x0801EFFF`.

The C-side single source of truth is `Core/Inc/memory_map.h`; the linker-side
equivalents are `Linker/mapping_sbsfu.ld` and `Linker/mapping_fwimg.ld` (kept
in sync by hand).

RAM: SE region `0x20000000`–`0x20000FFF` (4 K: key/crypto state incl. the CMOX
ECDSA working buffer, wiped before any exit from the bootloader), SBSFU region
above it. The Cortex-M0 has no VTOR:
the bootloader copies the app vector table to `0x20000000` and remaps SRAM to
`0x00000000` before the jump (same mechanism as the legacy bootloader), so the
application keeps `RAM ORIGIN = 0x200000C0`.

## Boot Flow

1. `main()`: clock (HSI×6 = 48 MHz, HSI48 → USB, LSI → IWDG/RTC), backup
   domain init, IWDG armed (~26 s).
2. App↔bootloader contract in RTC backup registers (unchanged from the legacy
   bootloader — existing applications only need a relink):
   - `BKP0R` = `'OWBL'` signature
   - `BKP1R` = `'DFU!'` one-shot force-DFU request (app writes it +
     `NVIC_SystemReset()`; bootloader consumes it)
   - `BKP2R` bits 8–15 = boot-fail counter: incremented before each launch,
     cleared by the app once running (`bootloader_mark_boot_ok()`) and on any
     DFU erase; at 3 failures the bootloader stops retrying and waits in DFU.
3. `SFU_BOOT_RunSecureBootService()`: SE init → detect FW → verify header
   signature + FwTag → launch (does not return). Before the jump the
   bootloader wipes the SE key RAM, resets the RCC to its power-on state
   (HSI 8 MHz — the app's clock init requires a virgin RCC), stops SysTick,
   clears NVIC and jumps through the SRAM-remapped vector table.
4. No valid firmware (or forced DFU / fail counter): USB DFU download mode
   (VID:PID `0483:DF11`), blue LED blinking. After a completed download the
   device checks the DFU-level anti-rollback (new `FwVersion` must not be
   lower than the installed one; downgrades are invalidated on the spot) and
   reboots into verification.

Trace console: USART3 PC10/PC11, 115200 8N1 (ST-Link VCP).

## Building

```
cmake --preset Release && cmake --build build/Release
cmake --preset Debug   && cmake --build build/Debug     # -Os -g3 (must fit 64K)
```

Requires arm-none-eabi GCC and Ninja. LTO is deliberately OFF: the linker
script places the SE interface and SE statics by object-file name, which
`-flto` silently breaks (the SE call gate then resets the MCU).

Before the first build generate the product keys (gitignored):

```
python py-tools/generate_keys.py          # writes py-tools/keys/ + se_key.s
```

`se_key.s` is generated in Cortex-M0 (Thumb-1) encoding — `MOVW/MOVT` do not
exist on ARMv6-M.

## Preparing an Application

1. Link at `0x08010400`, max 59 K (`STM32F072XX_FLASH_BL.ld` in
   openlifu-console-fw — presets `DebugBL` / `ReleaseBL`).
2. Keep the backup-register contract (already implemented in
   openlifu-console-fw: `bootloader_mark_boot_ok()` in `main()`, and the DFU
   command writes `BKP1R='DFU!'` + reset).
3. Keep all persistent app data OUTSIDE the slot (`>= 0x0801F000`) — see the
   memory-map warning above. The console app already uses `0x0801F000`.
4. Sign:
   ```
   python py-tools/sign_firmware.py --firmware app.bin --version N --output app_signed.bin
   ```
5. Install over USB DFU (any of):
   ```
   dfu-util -a 0 -s 0x08010000 -D app_signed.bin
   python py-tools/flash_firmware.py app_signed.bin        # full-slot erase (recommended)
   ```

## Factory / Bring-up Procedure (validated on hardware 2026-07-10)

1. Flash the bootloader with ST-Link:
   `STM32_Programmer_CLI -c port=SWD mode=UR -w build/Release/lifu-console-bl.hex -v`
2. Reset. With an empty slot the device prints the SBSFU banner on the trace
   UART and enumerates as USB DFU.
3. Download the signed application (see above). The device reboots, verifies
   the signature (< 1 s) and launches the app.
4. App-requested DFU: the application writes `'DFU!'` to `BKP1R` and resets —
   the bootloader consumes the request and waits in DFU even with a valid app
   installed.

Note for debugging: connecting a debugger while the device sits in silent DFU
(fail counter exhausted) is normal after repeated debug resets — the counter
counts every launch attempt the app did not acknowledge. Clear `BKP2R` or
perform a DFU erase to re-arm launches.

## Production hardening (deferred, tracked)

- WRP over the bootloader pages + RDP level 1/2 option-byte pass
  (`SFU_WRP_PROTECT_ENABLE` / `SFU_RDP_PROTECT_ENABLE` are implemented for the
  F0 but compiled out by `SECBOOT_DISABLE_SECURITY_IPS` in `app_sfu.h`).
- The F072 has **no MPU, no PCROP, no firewall**: enabling those ST switches
  raises a compile error. Key confidentiality relies on RDP + the SE-RAM wipe.
- Flash-persistent anti-rollback floor: not present (no spare page); DFU-level
  downgrade rejection is active. See `SBSFU/Target/Src/anti_rollback.c`.
