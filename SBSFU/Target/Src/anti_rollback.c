/**
  ******************************************************************************
  * @file    anti_rollback.c
  * @brief   Anti-rollback version floor — STM32F072 port.
  *
  *          The H743 reference persists a monotonic version floor in a
  *          dedicated 128K flash sector outside the DFU window. The F072 has
  *          128K of flash TOTAL and no spare page: the active slot already
  *          uses every page between the bootloader and the user-config page,
  *          so there is nowhere to keep a flash-persistent floor without
  *          shrinking the application slot below the current firmware size.
  *
  *          This port therefore keeps the API but with NO persistent floor:
  *            - AntiRollback_GetFloor() returns 0 (every signed version boots)
  *            - AntiRollback_UpdateFloor() is a no-op
  *
  *          Rollback protection that REMAINS active:
  *            - DFU-time downgrade rejection (usbd_dfu_if.c): a downloaded
  *              image with a FwVersion lower than the installed one is
  *              invalidated before it can ever boot. Combined with the
  *              read-only DFU bootloader window this covers the practical
  *              downgrade path (USB DFU is the only field update channel).
  *
  *          If a hard flash-persistent floor is required later, the options
  *          are: reserve one 2K page by shrinking the slot to 84K (breaks
  *          Debug-size images), or pack the floor into the user-config page
  *          alongside the application settings.
  ******************************************************************************
  */

#include "anti_rollback.h"

/**
  * @brief  Return the current anti-rollback version floor.
  * @retval Always 0 on this target (no persistent floor — see file header).
  */
uint32_t AntiRollback_GetFloor(void)
{
  return 0U;
}

/**
  * @brief  Raise the anti-rollback version floor.
  * @param  version: verified version of the image being launched (unused).
  * @retval None. No-op on this target (no persistent floor — see file header).
  */
void AntiRollback_UpdateFloor(uint32_t version)
{
  (void)version;
}
