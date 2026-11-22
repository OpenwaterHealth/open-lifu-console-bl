/*
 * memory_map.h
 *
 * Flash partition map for the open-lifu console secure bootloader
 * (STM32F072xB: 128K flash in 64 x 2K pages, 16K RAM).
 *
 * THIS FILE IS THE SINGLE SOURCE OF TRUTH for the C side of the partition
 * layout. The linker-side equivalents live in Linker/mapping_sbsfu.ld and
 * Linker/mapping_fwimg.ld and MUST be kept in sync.
 *
 *  pages  0-31   0x08000000   64K  BOOTLOADER (read-only over DFU)
 *  pages 32-60   0x08010000   58K  APP SLOT 1 (active; DFU erase/write/read)
 *  page  61      0x0801E800    2K  PRESETS (signed preset package; DFU writes
 *                                  it only after its PRS1 header verifies;
 *                                  outside the slot so VerifyActiveSlot
 *                                  ignores it)
 *  page  62      0x0801F000    2K  LIFU APP CONFIG (lifu_config.c "LIFU" block:
 *                                  serial number and calibration, written by
 *                                  the app at run time; not DFU-accessible)
 *  page  63      0x0801F800    2K  ANTI-ROLLBACK FLOOR (anti_rollback.c;
 *                                  not DFU-accessible)
 *
 * The application finds this layout in the bootloader info block (BL_INFO_*
 * below), so it never writes page 61 under an older bootloader whose slot
 * still covered it.
 *
 * Signed image layout inside the slot (py-tools/sign_firmware.py):
 *  0x08010000  SFU1 header (0x140 bytes) + 0xFF padding
 *  0x08010400  application image (executes here)
 *
 * The application must therefore link at 0x0800A400 and, because the
 * Cortex-M0 has no VTOR, copy its vector table to 0x20000000 and remap
 * SRAM to 0x00000000 (SYSCFG) during startup — same mechanism as the
 * legacy bootloader, only the base address changes.
 */

#ifndef INC_MEMORY_MAP_H_
#define INC_MEMORY_MAP_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_FLASH_BASE            ((uint32_t)0x08000000U)
#define MEM_FLASH_SIZE            ((uint32_t)0x00020000U)  /* 128K */
#define MEM_FLASH_END             (MEM_FLASH_BASE + MEM_FLASH_SIZE)
#define MEM_PAGE_SIZE             ((uint32_t)0x00000800U)  /* 2K */

/* Bootloader: pages 0-31 */
#define MEM_BOOTLOADER_BASE       ((uint32_t)0x08000000U)
#define MEM_BOOTLOADER_SIZE       ((uint32_t)0x00010000U)  /* 64K */

/* Active application slot: pages 32-60 (must match mapping_fwimg.ld) */
#define MEM_SLOT1_BASE            ((uint32_t)0x08010000U)
#define MEM_SLOT1_SIZE            ((uint32_t)0x0000E800U)  /* 58K */
#define MEM_SLOT1_END             (MEM_SLOT1_BASE + MEM_SLOT1_SIZE)

/* Preset page: page 61, right above the slot. Holds one signed preset
 * package: a PRS1 header laid out like the SFU1 header (signed with the same
 * key; magic, body size and the body's SHA-256 in FwTag inside the signed
 * part), then the preset records at MEM_PRESET_BODY_OFFSET. */
#define MEM_PRESET_BASE           ((uint32_t)0x0801E800U)
#define MEM_PRESET_SIZE           (MEM_PAGE_SIZE)
#define MEM_PRESET_END            (MEM_PRESET_BASE + MEM_PRESET_SIZE)
#define MEM_PRESET_BODY_OFFSET    ((uint32_t)0x00000140U)  /* sizeof(SE_FwRawHeaderTypeDef) */

/* LIFU application runtime-config page: page 62, written by the application
 * (lifu_config.c). Outside the slot and outside the DFU window. */
#define MEM_APP_CONFIG_BASE       ((uint32_t)0x0801F000U)
#define MEM_APP_CONFIG_SIZE       (MEM_PAGE_SIZE)

/* Offset of the executable image inside the slot (SFU1 header + padding).
 * Must match SFU_IMG_IMAGE_OFFSET and sign_firmware.py IMAGE_OFFSET. */
#define MEM_APP_IMAGE_OFFSET      ((uint32_t)0x00000400U)
#define MEM_APP_RUN_ADDRESS       (MEM_SLOT1_BASE + MEM_APP_IMAGE_OFFSET)  /* 0x08010400 */

/* Anti-rollback floor: page 63 (anti_rollback.c), preserved across updates */
#define MEM_ANTIROLLBACK_BASE     ((uint32_t)0x0801F800U)
#define MEM_ANTIROLLBACK_SIZE     (MEM_PAGE_SIZE)

/* DFU-writable window (usbd_dfu_if.c enforces this on erase/write/read) */
#define MEM_DFU_WRITABLE_BASE     (MEM_SLOT1_BASE)
#define MEM_DFU_WRITABLE_END      (MEM_SLOT1_END)          /* exclusive */

/* Bootloader info block: a fixed place, in the padding after the vector
 * table, where the application reads this layout (bl_info.c). Bootloaders
 * before 1.1.0-rc.2 have only fill bytes here, so the magic tells them apart. */
#define BL_INFO_ADDRESS           ((uint32_t)0x080000E0U)
#define BL_INFO_MAGIC             ((uint32_t)0x4942574FU)  /* 'OWBI' */
#define BL_INFO_LAYOUT            (1U)

/* DFU capability word: served at the DFU virtual capability address (behind
 * the "OWC1" magic, usbd_dfu_if.c) and copied into the info block. */
#define BL_CAP_VERIFY_BEFORE_ERASE   (0x00000001U)  /* stage + verify the SFU1 header first */
#define BL_CAP_SIGNED_PRESET_WRITE   (0x00000002U)  /* page 61 takes a signed PRS1 package */
#define BL_CAPS                      (BL_CAP_VERIFY_BEFORE_ERASE | BL_CAP_SIGNED_PRESET_WRITE)

typedef struct
{
  uint32_t magic;          /* BL_INFO_MAGIC */
  uint16_t layout;         /* BL_INFO_LAYOUT */
  uint16_t reserved;
  uint32_t slot_base;
  uint32_t slot_end;       /* exclusive */
  uint32_t preset_base;
  uint32_t preset_size;
  uint32_t caps;           /* the DFU capability word (usbd_dfu_if.c) */
  uint32_t reserved2;
} bl_info_t;               /* 32 bytes */

/* SRAM */
#define MEM_SRAM_BASE             ((uint32_t)0x20000000U)
#define MEM_SRAM_SIZE             ((uint32_t)0x00004000U)  /* 16K */

/*
 * Persistent boot state lives in the RTC backup registers (BKP0R..BKP4R).
 * This is the app <-> bootloader contract inherited from the legacy
 * bootloader — the existing application already implements its side.
 *
 *  BKP0R  'OWBL' signature: marks the register bank as initialized
 *  BKP1R  one-shot DFU request: app writes 'DFU!' + NVIC_SystemReset() to
 *         enter THIS (Open-LIFU) bootloader's USB DFU. The bootloader consumes
 *         whatever it holds on every boot and ignores any other word (older
 *         apps wrote 'DFUS' for the STM32 ROM loader, which no build of this
 *         bootloader enters).
 *  BKP2R  boot state: bits 8-15 = boot-attempt fail counter (bootloader
 *         increments before launch; app clears on successful boot)
 *  BKP3R  reserved (legacy: last bad fw marker; app clears it)
 *  BKP4R  reserved (legacy auth cache; unused by the SBSFU bootloader)
 */
#define BL_BKP_SIGNATURE              ((uint32_t)0x4F57424CU) /* 'OWBL' */
#define BL_BKP_REQ_DFU_MAGIC          ((uint32_t)0x21554644U) /* 'DFU!' -> Open-LIFU DFU */
#define BL_BKP_STATE_FAILCOUNT_SHIFT  (8U)
#define BL_BKP_STATE_FAILCOUNT_MASK   ((uint32_t)0xFFU << BL_BKP_STATE_FAILCOUNT_SHIFT)
#define BL_BOOT_FAIL_MAX              (3U)

#ifdef __cplusplus
}
#endif

#endif /* INC_MEMORY_MAP_H_ */
