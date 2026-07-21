/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_dfu_if.c
  * @brief          : Usb device for Download Firmware Update.
  *                   SBSFU secure bootloader media interface — STM32F072.
  *                   Ported from the open-motion-console-bl (STM32H743)
  *                   reference: window-bounded erase/write/read, version
  *                   query via a virtual UPLOAD address, DFU-level
  *                   anti-rollback against the installed slot version.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_dfu_if.h"

/* USER CODE BEGIN INCLUDE */
#include "main.h"
#include "memory_map.h"
#include "version.h"
#include <string.h>

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* Set once a new image has been programmed in this DFU session; the main loop
 * polls DFU_ImageDownloadComplete() and reboots into the new image after the
 * host finishes manifestation. */
static uint8_t  s_dfu_image_written  = 0U;

/* Anti-rollback: version of the image that was installed when the DFU session
 * started, latched on the first erase (before the header is destroyed). */
static uint8_t  s_cur_ver_captured   = 0U;
static uint16_t s_current_fw_version = 0U;

/* Set when the host writes to DFU_RESET_VIRT_ADDR (see MEM_If_Write_FS). The
 * bootloader main loop polls DFU_ResetRequested() and performs the reset. */
static volatile uint8_t s_dfu_reset_requested = 0U;

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device.
  * @{
  */

/** @defgroup USBD_DFU
  * @brief Usb DFU device module.
  * @{
  */

/** @defgroup USBD_DFU_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_Defines
  * @brief Private defines.
  * @{
  */

/* STM32F072xB: 128K flash, single bank, 64 x 2K pages. See Core/Inc/memory_map.h.
 * DfuSe sector type letters encode access (bit0=read, bit1=erase, bit2=write):
 *   'a' = 1 = read-only,  'g' = 7 = read+erase+write.
 *   pages  0-31    bootloader (incl. SE keys)   read-only  -> 32*2Ka
 *   pages 32-61    active app slot              r/w/erase  -> 30*2Kg
 *   page  62       LIFU app runtime config      read-only  -> (part of 02*2Ka)
 *   page  63       user config                  read-only  -> (part of 02*2Ka)
 * The DFU writable window is clamped to the SBSFU active slot
 * (SLOT_ACTIVE_1: 0x08010000-0x0801EFFF, see Linker/mapping_fwimg.ld) so that
 * ALL DFU-writable flash is covered by secure-boot slot verification. */
#define FLASH_DESC_STR      "@Internal Flash/0x08000000/32*2Ka,30*2Kg,02*2Ka"

/* DFU writable window: the active application slot only. The bootloader (below
 * APP_FLASH_BASE) and everything at/above FLASH_END_ADDR (the user-config page)
 * are excluded and cannot be erased, written, or read over DFU. */
#define APP_FLASH_BASE      MEM_DFU_WRITABLE_BASE   /* 0x0800A000 (active slot start) */
#define FLASH_END_ADDR      MEM_DFU_WRITABLE_END    /* 0x0801F800 (slot end + 1, exclusive) */

/* USER CODE BEGIN PRIVATE_DEFINES */

/* Virtual DFU UPLOAD address (outside flash). A host that points the DfuSe
 * address pointer here and uploads receives the bootloader version string
 * (FW_VERSION from version.h), null-padded to DFU_VERSION_READ_LEN bytes.
 * This is a read-only query: nothing is written and no flash is touched. */
#define DFU_VERSION_VIRT_ADDR   0xFFFFFF00U
#define DFU_VERSION_READ_LEN    64U

/* Virtual DFU DNLOAD address (outside flash). A host that points the DfuSe
 * address pointer here and downloads any payload requests a device reset —
 * the clean way to leave DFU mode without flashing (e.g. the SDK aborting an
 * update after its pre-flight downgrade check). No flash is touched: the
 * request is only latched here and the bootloader main loop performs the
 * reset after the host's final GETSTATUS handshake completes. SBSFU fully
 * re-verifies the slot on the way back up, so this can never launch an
 * unverified image. */
#define DFU_RESET_VIRT_ADDR     0xFFFFFF08U

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_Variables
  * @brief Private variables.
  * @{
  */

/* USER CODE BEGIN PRIVATE_VARIABLES */

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_DFU_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static uint16_t MEM_If_Init_FS(void);
static uint16_t MEM_If_Erase_FS(uint32_t Add);
static uint16_t MEM_If_Write_FS(uint8_t *src, uint8_t *dest, uint32_t Len);
static uint8_t *MEM_If_Read_FS(uint8_t *src, uint8_t *dest, uint32_t Len);
static uint16_t MEM_If_DeInit_FS(void);
static uint16_t MEM_If_GetStatus_FS(uint32_t Add, uint8_t Cmd, uint8_t *buffer);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static uint16_t dfu_read_slot_version(void);
/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

#if defined ( __ICCARM__ ) /* IAR Compiler */
  #pragma data_alignment=4
#endif
__ALIGN_BEGIN USBD_DFU_MediaTypeDef USBD_DFU_fops_FS __ALIGN_END =
{
   (uint8_t*)FLASH_DESC_STR,
    MEM_If_Init_FS,
    MEM_If_DeInit_FS,
    MEM_If_Erase_FS,
    MEM_If_Write_FS,
    MEM_If_Read_FS,
    MEM_If_GetStatus_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Memory initialization routine.
  * @retval USBD_OK if operation is successful, MAL_FAIL else.
  */
uint16_t MEM_If_Init_FS(void)
{
  /* USER CODE BEGIN 0 */
  return (USBD_OK);
  /* USER CODE END 0 */
}

/**
  * @brief  De-Initializes Memory
  * @retval USBD_OK if operation is successful, MAL_FAIL else
  */
uint16_t MEM_If_DeInit_FS(void)
{
  /* USER CODE BEGIN 1 */
  return (USBD_OK);
  /* USER CODE END 1 */
}

/**
  * @brief  Erase page.
  * @param  Add: Address of page to be erased.
  * @retval USBD_OK if operation is successful, USBD_FAIL else.
  */
uint16_t MEM_If_Erase_FS(uint32_t Add)
{
  /* USER CODE BEGIN 2 */

  /* Reject erase outside the active application slot (protects the bootloader,
   * the SE key material and the user-config page) */
  if ((Add < APP_FLASH_BASE) || (Add >= FLASH_END_ADDR))
  {
    return (USBD_FAIL);
  }

  /* Anti-rollback: latch the currently-installed version BEFORE any erase wipes
   * the active-slot header. Done once per DFU session, on the first erase. */
  if (s_cur_ver_captured == 0U)
  {
    s_cur_ver_captured   = 1U;
    s_current_fw_version = dfu_read_slot_version();
  }

  FLASH_EraseInitTypeDef eraseInit = {0};
  uint32_t pageError = 0U;
  uint32_t page_base = Add & ~(FLASH_PAGE_SIZE - 1U);
  uint32_t blank = 1U;
  uint32_t off;

  /* Skip already-blank pages: a real F0 page erase stalls the flash (and all
   * instruction fetches) for ~25 ms inside the USB interrupt, which is exactly
   * the window where interleaved erase/write DFU hosts (dfu-util DfuSe mode)
   * have been observed to corrupt the downloaded data on this device. The
   * recommended installer (py-tools/flash_firmware.py) erases every page
   * up-front, so with this check the write phase runs with no erase stalls. */
  for (off = 0U; off < FLASH_PAGE_SIZE; off += 4U)
  {
    if (*(uint32_t *)(page_base + off) != 0xFFFFFFFFU)
    {
      blank = 0U;
      break;
    }
  }

  HAL_StatusTypeDef status = HAL_OK;
  if (blank == 0U)
  {
    eraseInit.TypeErase   = FLASH_TYPEERASE_PAGES;
    eraseInit.PageAddress = page_base;
    eraseInit.NbPages     = 1U;

    HAL_FLASH_Unlock();
    status = HAL_FLASHEx_Erase(&eraseInit, &pageError);
    HAL_FLASH_Lock();
  }

  if (status == HAL_OK)
  {
    /* A new application image is being installed: clear the failsafe boot
     * counter (BKP2R bits 8-15) so the freshly flashed firmware gets a clean
     * set of boot attempts. Without this, if we entered DFU because the counter
     * hit BL_BOOT_FAIL_MAX, it would still be at the limit after the download
     * and the bootloader would refuse to launch the new image until a battery
     * removal. Backup-domain write access was already enabled in main(). */
    HAL_PWR_EnableBkUpAccess();
    RTC->BKP2R &= ~BL_BKP_STATE_FAILCOUNT_MASK;
    __DSB();
  }

  return (status == HAL_OK) ? USBD_OK : USBD_FAIL;

  /* USER CODE END 2 */
}

/**
  * @brief  Memory write routine.
  * @param  src:  Buffer containing data to program.
  * @param  dest: Target flash address.
  * @param  Len:  Number of bytes to write.
  * @retval USBD_OK if operation is successful, USBD_FAIL else.
  * @note   STM32F0 programs 32-bit words; a partial final word is padded
  *         with 0xFF.
  */
uint16_t MEM_If_Write_FS(uint8_t *src, uint8_t *dest, uint32_t Len)
{
  /* USER CODE BEGIN 3 */

  uint32_t addr = (uint32_t)dest;

  /* Virtual reset request: latch and ACK without touching flash. The main
   * loop resets after the host's status handshake completes. Deliberately
   * does NOT set s_dfu_image_written — no image state changes here. */
  if (addr == DFU_RESET_VIRT_ADDR)
  {
    s_dfu_reset_requested = 1U;
    return (USBD_OK);
  }

  /* Protect everything outside the active application slot */
  if ((addr < APP_FLASH_BASE) || (addr >= FLASH_END_ADDR) || (Len > (FLASH_END_ADDR - addr)))
  {
    return (USBD_FAIL);
  }

  uint32_t remaining = Len;

  HAL_FLASH_Unlock();

  while (remaining > 0U)
  {
    uint32_t word = 0xFFFFFFFFU;
    uint32_t chunk = (remaining >= 4U) ? 4U : remaining;
    memcpy(&word, src, chunk);

    if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, (uint64_t)word) != HAL_OK)
    {
      HAL_FLASH_Lock();
      return (USBD_FAIL);
    }

    src       += chunk;
    addr      += 4U;
    remaining -= chunk;
  }

  HAL_FLASH_Lock();

  s_dfu_image_written = 1U;   /* a new image was programmed: arm post-manifest reboot */

  return (USBD_OK);

  /* USER CODE END 3 */
}

/**
  * @brief  Memory read routine.
  * @param  src: Pointer to the source buffer. Address to be written to.
  * @param  dest: Pointer to the destination buffer.
  * @param  Len: Number of data to be read (in bytes).
  * @retval Pointer to the physical address where data should be read.
  */
uint8_t *MEM_If_Read_FS(uint8_t *src, uint8_t *dest, uint32_t Len)
{
  /* Return a valid address to avoid HardFault */
  /* USER CODE BEGIN 4 */

  /* DFU "get version" command: an UPLOAD from the virtual address returns the
   * bootloader version string instead of reading flash. */
  if ((uint32_t)src == DFU_VERSION_VIRT_ADDR)
  {
    const char *ver = FW_VERSION;
    uint32_t ver_len = (uint32_t)strlen(ver);
    if (ver_len > DFU_VERSION_READ_LEN) { ver_len = DFU_VERSION_READ_LEN; }
    if (ver_len > Len)                  { ver_len = Len; }
    memset(dest, 0, Len);
    memcpy(dest, ver, ver_len);
    return dest;
  }

  /* Bound DFU UPLOAD/read to the same window as erase/write: the active
   * application slot only. Without this, a host could point the DfuSe address
   * pointer at any address and UPLOAD-read it, exfiltrating the bootloader,
   * the SE key material (se_key.s in the first pages), the user-config page,
   * or RAM. The comparison against (FLASH_END_ADDR - addr) avoids the unsigned
   * overflow that (addr + Len) would risk. Returning NULL makes the DFU class
   * STALL the packet and NAK the command. */
  {
    uint32_t addr = (uint32_t)src;

    if ((addr < APP_FLASH_BASE) ||
        (addr >= FLASH_END_ADDR) ||
        (Len  > (FLASH_END_ADDR - addr)))
    {
      return (NULL);
    }
  }

  memcpy(dest, src, Len);
  return dest;
  /* USER CODE END 4 */
}

/**
  * @brief  Get status routine
  * @param  Add: Address to be read from
  * @param  Cmd: Number of data to be read (in bytes)
  * @param  buffer: used for returning the time necessary for a program or an erase operation
  * @retval USBD_OK if operation is successful
  */
uint16_t MEM_If_GetStatus_FS(uint32_t Add, uint8_t Cmd, uint8_t *buffer)
{
  /* USER CODE BEGIN 5 */
  UNUSED(Add);

  uint32_t timeout_ms;
  switch (Cmd)
  {
    case DFU_MEDIA_PROGRAM:
      timeout_ms = 20U;    /* 1024-byte block = 256 word programs on F0 */
      break;

    case DFU_MEDIA_ERASE:
      timeout_ms = 100U;   /* 2K page erase (typ. ~25 ms, generous margin) */
      break;

    default:
      timeout_ms = 0U;
      break;
  }

  /* wPollTimeout: 24-bit little-endian poll interval in ms */
  buffer[1] = (uint8_t)(timeout_ms & 0xFFU);
  buffer[2] = (uint8_t)((timeout_ms >> 8U) & 0xFFU);
  buffer[3] = (uint8_t)((timeout_ms >> 16U) & 0xFFU);

  return (USBD_OK);
  /* USER CODE END 5 */
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @brief  Reports whether a DFU download has completed and the host has
  *         finished the manifestation phase.
  * @note   The bootloader main loop polls this and reboots into the freshly
  *         programmed image once the host has cleanly read final status.
  * @retval 1 once an image was written AND manifestation is complete, else 0.
  */
uint8_t DFU_ImageDownloadComplete(void)
{
  if (s_dfu_image_written == 0U)
  {
    return 0U;
  }

  const USBD_DFU_HandleTypeDef *hdfu =
      (const USBD_DFU_HandleTypeDef *)hUsbDeviceFS.pClassData;
  if (hdfu == NULL)
  {
    return 0U;
  }

  /* Manifestation is finished once DFU_Leave() has run (manif_state COMPLETE).
   * dfu-util in DfuSe mode leaves us in MANIFEST_SYNC; a host that polls to the
   * end leaves us in dfuIDLE. Accept either. The s_dfu_image_written gate keeps
   * this from matching the power-on default state. */
  if (hdfu->manif_state != DFU_MANIFEST_COMPLETE)
  {
    return 0U;
  }

  return ((hdfu->dev_state == DFU_STATE_MANIFEST_SYNC) ||
          (hdfu->dev_state == DFU_STATE_IDLE)) ? 1U : 0U;
}

/**
  * @brief  Read the FwVersion from the SBSFU header at the active slot start.
  * @note   Header layout (see py-tools/sign_firmware.py): "SFU1" magic at 0x000,
  *         ProtocolVersion at 0x004, FwVersion (u16, little-endian) at 0x006.
  * @retval The installed FwVersion, or 0 if there is no valid "SFU1" header.
  */
static uint16_t dfu_read_slot_version(void)
{
  const uint8_t *hdr = (const uint8_t *)APP_FLASH_BASE;

  if ((hdr[0] != 'S') || (hdr[1] != 'F') || (hdr[2] != 'U') || (hdr[3] != '1'))
  {
    return 0U;   /* no valid installed image -> no rollback floor */
  }
  return (uint16_t)((uint16_t)hdr[6] | ((uint16_t)hdr[7] << 8));
}

/**
  * @brief  Anti-rollback check on the just-downloaded image.
  * @note   Compares the new image's FwVersion (read from its freshly written
  *         header) against the version installed at DFU entry. The version field
  *         is inside the ECDSA-signed header region, so an attacker cannot forge
  *         a higher value on a genuinely-signed old image; a forged-high but
  *         unsigned image is rejected by SBSFU at boot anyway.
  * @retval 1 if the new image is a downgrade (must be refused), else 0.
  */
uint8_t DFU_IsRollback(void)
{
  uint16_t new_ver = dfu_read_slot_version();

  if (new_ver == 0U)
  {
    /* No valid header written: not a "rollback" — SBSFU's signature check will
     * reject it at boot anyway. */
    return 0U;
  }
  return (new_ver < s_current_fw_version) ? 1U : 0U;
}

/**
  * @brief  Destroy the image in the active slot by erasing its header page.
  * @note   Used to make a rejected downgrade unbootable: without a valid header
  *         SBSFU refuses to launch it.
  */
void DFU_InvalidateImage(void)
{
  (void)MEM_If_Erase_FS(APP_FLASH_BASE);
}

/**
  * @brief  Clear the post-download state so the main loop stops acting on a
  *         completed download (used after a rollback has been handled).
  */
void DFU_ClearDownloadState(void)
{
  s_dfu_image_written = 0U;
}

/**
  * @brief  Reports whether the host requested a device reset via a DNLOAD to
  *         DFU_RESET_VIRT_ADDR. Polled by the bootloader main loop, which
  *         performs the actual NVIC_SystemReset (after a short delay so the
  *         host's final USB status transaction completes).
  * @retval 1 if a reset was requested, else 0.
  */
uint8_t DFU_ResetRequested(void)
{
  return s_dfu_reset_requested;
}

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */
