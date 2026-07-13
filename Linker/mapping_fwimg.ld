/*
*****************************************************************************
**  File        : mapping_fwimg.ld
**  Abstract    : Firmware image slot mapping for single-image SBSFU on
**                STM32F072xB (128K flash, 2K pages).
*****************************************************************************
*/

/* Flash partition map (see Core/Inc/memory_map.h for the C-side view):
 *   pages  0-31   0x08000000   64K  bootloader (read-only over DFU)
 *   pages 32-61   0x08010000   60K  APP SLOT 1 (active)   <-- this file
 *   page  62      0x0801F000    2K  LIFU app runtime config (written by the
 *                                   app at run time — lifu_config.c "LIFU"
 *                                   block; MUST stay outside the slot or
 *                                   VerifyActiveSlot fails once the app has
 *                                   saved its settings)
 *   page  63      0x0801F800    2K  user config (preserved, not DFU-writable)
 *
 * Signed image layout inside the slot (sign_firmware.py):
 *   0x08010000  SFU1 header (0x140) + 0xFF pad
 *   0x08010400  application (executes here; SFU_IMG_IMAGE_OFFSET = 0x400)
 */

/* Active slot #1: pages 32-61 = 30 x 2K = 60K */
__ICFEDIT_SLOT_Active_1_header__ = 0x08010000;
__ICFEDIT_SLOT_Active_1_start__  = 0x08010000;
__ICFEDIT_SLOT_Active_1_end__    = 0x0801EFFF;

/* Slots 2 and 3 are not managed in this single-image configuration. */
__ICFEDIT_SLOT_Active_2_header__ = 0x00000000;
__ICFEDIT_SLOT_Active_2_start__  = 0x00000000;
__ICFEDIT_SLOT_Active_2_end__    = 0x00000000;
__ICFEDIT_SLOT_Active_3_header__ = 0x00000000;
__ICFEDIT_SLOT_Active_3_start__  = 0x00000000;
__ICFEDIT_SLOT_Active_3_end__    = 0x00000000;
