/**
  ******************************************************************************
  * @file    anti_rollback.c
  * @brief   Anti-rollback version floor — STM32F072 port.
  *
  * Persists the monotonic version floor in the USER CONFIG flash page
  * (page 63, MEM_USER_CONFIG_BASE = 0x0801F800, 2 KB):
  *   - outside the DFU-writable window [MEM_DFU_WRITABLE_BASE,
  *     MEM_DFU_WRITABLE_END) enforced by usbd_dfu_if.c, so a downgrade
  *     image can never erase the floor over USB;
  *   - unused by the bootloader and by the application (the app's runtime
  *     config lives in page 62 and its erase stops at this page);
  *   - only reachable with a debug probe (which production RDP locks out).
  *
  * Storage format: an append-only array of uint16_t records. Each record
  * stores the BITWISE COMPLEMENT of a FwVersion, so erased flash (0xFFFF)
  * can never alias a record: that would require version 0, which the boot
  * path and the signing tools both forbid. The floor is the maximum
  * version over all records.
  *
  * Raising the floor programs ONE new halfword into blank space - no page
  * erase - so updates are power-cut-safe (a torn write at worst loses the
  * raise, never lowers the floor) and wear-free. The page is erased and
  * rewritten with a single record only when all 1024 record slots are
  * used, i.e. after 1024 distinct version raises (practically never).
  *
  * Both functions are best-effort by design (see anti_rollback.h): a flash
  * failure leaves the previous floor in place, which is fail-safe.
  ******************************************************************************
  */

#include "anti_rollback.h"
#include "memory_map.h"
#include "stm32f0xx_hal.h"

#define AR_PAGE_BASE   MEM_USER_CONFIG_BASE
#define AR_PAGE_SIZE   MEM_USER_CONFIG_SIZE
#define AR_NUM_SLOTS   (AR_PAGE_SIZE / 2U)      /* 1024 uint16_t records */
#define AR_BLANK       ((uint16_t)0xFFFFU)

/* Record slot idx as stored in flash (complemented version, AR_BLANK = empty) */
static uint16_t ar_read_slot(uint32_t idx)
{
  return *(volatile const uint16_t *)(AR_PAGE_BASE + (idx * 2U));
}

/* Index of the first blank record slot, or AR_NUM_SLOTS if the page is full */
static uint32_t ar_first_blank_slot(void)
{
  uint32_t idx;

  for (idx = 0U; idx < AR_NUM_SLOTS; idx++)
  {
    if (ar_read_slot(idx) == AR_BLANK)
    {
      break;
    }
  }
  return idx;
}

/* Program one record. Returns 1 on verified success, 0 otherwise. */
static uint32_t ar_program_record(uint32_t idx, uint16_t version)
{
  uint32_t addr = AR_PAGE_BASE + (idx * 2U);
  uint16_t record = (uint16_t)~version;
  HAL_StatusTypeDef status;

  (void)HAL_FLASH_Unlock();
  status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, record);
  (void)HAL_FLASH_Lock();

  return ((status == HAL_OK) && (ar_read_slot(idx) == record)) ? 1U : 0U;
}

/* Erase the record page. Returns 1 on success, 0 otherwise. */
static uint32_t ar_erase_page(void)
{
  FLASH_EraseInitTypeDef erase = {0};
  uint32_t page_error = 0U;
  HAL_StatusTypeDef status;

  erase.TypeErase   = FLASH_TYPEERASE_PAGES;
  erase.PageAddress = AR_PAGE_BASE;
  erase.NbPages     = 1U;

  (void)HAL_FLASH_Unlock();
  status = HAL_FLASHEx_Erase(&erase, &page_error);
  (void)HAL_FLASH_Lock();

  return (status == HAL_OK) ? 1U : 0U;
}

/**
  * @brief  Return the highest firmware version ever launched (the rollback
  *         floor), or 0 if none has been recorded yet.
  * @note   Scans all records and returns the maximum, which makes the result
  *         robust against torn writes and unordered content.
  */
uint32_t AntiRollback_GetFloor(void)
{
  uint32_t floor = 0U;
  uint32_t idx;

  for (idx = 0U; idx < AR_NUM_SLOTS; idx++)
  {
    uint16_t record = ar_read_slot(idx);
    if (record != AR_BLANK)
    {
      uint16_t version = (uint16_t)~record;
      if ((uint32_t)version > floor)
      {
        floor = (uint32_t)version;
      }
    }
  }
  return floor;
}

/**
  * @brief  Raise the anti-rollback version floor to @p version if it is
  *         strictly higher than the current floor; otherwise no-op.
  * @param  version: FwVersion read from a signature-verified image header
  *         (1..65535; 0 and out-of-range values are ignored).
  * @retval None. Best-effort: on any flash failure the previous floor
  *         remains in effect, which is fail-safe.
  */
void AntiRollback_UpdateFloor(uint32_t version)
{
  uint32_t idx;

  if ((version == 0U) || (version > 0xFFFFU))
  {
    return;
  }
  if (version <= AntiRollback_GetFloor())
  {
    return;
  }

  idx = ar_first_blank_slot();
  if (idx >= AR_NUM_SLOTS)
  {
    /* Page full after 1024 raises: compact to a single record. A power cut
     * between the erase and the program loses the floor - accepted, as this
     * path is practically unreachable and the next boot re-latches it. */
    if (ar_erase_page() == 0U)
    {
      return;
    }
    idx = 0U;
  }

  (void)ar_program_record(idx, (uint16_t)version);
}
