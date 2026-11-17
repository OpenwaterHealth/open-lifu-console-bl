# STM32 USB Device Library — local changes

SOUP item: STMicroelectronics `stm32-mw-usb-device`
(https://github.com/STMicroelectronics/stm32-mw-usb-device).

Upstream version in this tree: **v2.11.6** (`Core/` and `Class/DFU/`; only the DFU
class is built). This is the version pinned by STM32CubeF0 v1.12.0.

Updated from v2.5.3 on 2026-11-16. v2.5.3 passed the host's `wLength` unbounded into
the 1024-byte DFU transfer buffer (`DFU_Download`, `DFU_Upload`) and on into
`MEM_If_Read_FS`. On the bench console a single DFU UPLOAD with `wLength` 4096 crashed
bootloader 1.0.3-rc.1 (it dropped off the bus). Upstream fixed this in v2.8.0
("Fix USB buffer overflow vulnerability for ... DFU ..."). v2.11.6 also brings the
`usbd_ctlreq.c` fixes: the endpoint index in `USBD_StdEPReq` is checked against 15,
and `USBD_GetString` is bounded by the destination length.

Every file is byte-identical to upstream v2.11.6 except the one listed below. Verify
with a `diff` against the upstream tag before recording a new version here. The
generator `scripts/gen_sbom.py` pins these files by hash (`scripts/sbom-manifest.json`),
so any further change fails CI until the manifest is reviewed.

## Class/DFU/Src/usbd_dfu.c — `USBD_DFU_EP0_TxReady`

1. **DfuSe 1-byte mass erase (`0x41`, no address) → `errERASE`.** With vendor commands
   disabled, upstream treats it as a silent no-op. This bootloader does not support
   mass erase (only the active slot may be erased, after a verified header), so the
   host gets an explicit error instead of believing the flash was erased.
2. **Erase failure → `errERASE`; write failure → `errWRITE`.** Upstream reports
   `errVENDOR` for both. The host tools (`py-tools/stm32dfu.py`, openlifu-sdk) report
   these codes; a refused verify-before-erase header arrives as `errWRITE`.

Both are functional changes, not security fixes, and must be re-applied on every
library update.

## Glue outside this directory (CubeMX `USB_DEVICE/Target`)

- `usbd_conf.c`: `USBD_LL_Transmit` / `USBD_LL_PrepareReceive` take a `uint32_t` size
  (v2.11 API).
- `usbd_conf.h`: `USBD_memset` / `USBD_memcpy` alias `memset` / `memcpy`. CubeMX
  generated them as empty for the old library; v2.11 classes zero their handle with
  `USBD_memset` on every class init.
