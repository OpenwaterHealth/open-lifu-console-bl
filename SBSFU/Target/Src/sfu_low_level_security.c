/**
  ******************************************************************************
  * @file    sfu_low_level_security.c
  * @author  MCD Application Team
  * @brief   SFU Security Low Level Interface module — STM32F072 port.
  *          This file provides set of firmware functions to manage SFU security
  *          low level interface.
  *
  *          The F072 has no MPU / PCROP / Firewall / secure-user memory
  *          (compile guards in sfu_low_level_security.h reject those switches).
  *          WRP and RDP are implemented for the future production option-byte
  *          pass; in the current development configuration
  *          (SECBOOT_DISABLE_SECURITY_IPS) every protection is compiled out.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2017 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file in
  * the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "sfu_low_level_security.h"
#include "sfu_low_level_flash_int.h"
#include "sfu_low_level.h"
#include "sfu_trace.h"
#include "sfu_boot.h"
#include "sfu_fsm_states.h"
#include "sfu_error.h"

#ifndef  SFU_WRP_PROTECT_ENABLE
#if !defined(SECBOOT_DISABLE_SECURITY_IPS)
#warning "SFU_WRP_PROTECT_DISABLED"
#endif /* SECBOOT_DISABLE_SECURITY_IPS */
#endif /* SFU_WRP_PROTECT_ENABLE */

#ifndef  SFU_RDP_PROTECT_ENABLE
#if !defined(SECBOOT_DISABLE_SECURITY_IPS)
#warning "SFU_RDP_PROTECT_DISABLED"
#endif /* SECBOOT_DISABLE_SECURITY_IPS */
#endif /* SFU_RDP_PROTECT_ENABLE */

#ifndef  SFU_IWDG_PROTECT_ENABLE
#if !defined(SECBOOT_DISABLE_SECURITY_IPS)
#warning "SFU_IWDG_PROTECT_DISABLED"
#endif /* SECBOOT_DISABLE_SECURITY_IPS */
#endif /* SFU_IWDG_PROTECT_ENABLE */

#if defined(SFU_DAP_PROTECT_ENABLE)
#error "The STM32F072 has no option byte to disable the debug port: use RDP level 2 instead."
#endif /* SFU_DAP_PROTECT_ENABLE */

#if defined(SFU_TAMPER_PROTECT_ENABLE)
#error "Anti-tamper protection is not ported to the STM32F072 bootloader yet."
#endif /* SFU_TAMPER_PROTECT_ENABLE */

/* Private typedef -----------------------------------------------------------*/
/**
  * @brief  Standard boolean used by the option-byte handling below.
  */
typedef enum
{
  SFU_FALSE = 0U,
  SFU_TRUE = !SFU_FALSE
} SFU_BoolTypeDef;

/* Private variables ---------------------------------------------------------*/
#ifdef SFU_IWDG_PROTECT_ENABLE
static IWDG_HandleTypeDef   IwdgHandle;
#endif /* SFU_IWDG_PROTECT_ENABLE */

/* Private function prototypes -----------------------------------------------*/
#ifdef SFU_WRP_PROTECT_ENABLE
static uint32_t        SFU_LL_SECU_GetWRPMask(void);
static SFU_ErrorStatus SFU_LL_SECU_CheckProtectionWRP(FLASH_OBProgramInitTypeDef *psFlashOptionBytes);
static SFU_ErrorStatus SFU_LL_SECU_SetProtectionWRP(FLASH_OBProgramInitTypeDef *psFlashOptionBytes,
                                                    SFU_BoolTypeDef *pbIsProtectionToBeApplied);
#endif /* SFU_WRP_PROTECT_ENABLE */
#ifdef SFU_RDP_PROTECT_ENABLE
static SFU_ErrorStatus SFU_LL_SECU_SetProtectionRDP(FLASH_OBProgramInitTypeDef *psFlashOptionBytes,
                                                    SFU_BoolTypeDef *pbIsProtectionToBeApplied);
#endif /* SFU_RDP_PROTECT_ENABLE */
#ifdef SFU_IWDG_PROTECT_ENABLE
static SFU_ErrorStatus SFU_LL_SECU_SetProtectionIWDG(void);
#endif /* SFU_IWDG_PROTECT_ENABLE */

/* Functions Definition ------------------------------------------------------*/

/**
  * @brief  Check and if not applied apply the Static security protections.
  *         Only static protections involving option bytes are handled here.
  * @param  None
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
SFU_ErrorStatus SFU_LL_SECU_CheckApplyStaticProtections(void)
{
  FLASH_OBProgramInitTypeDef flash_option_bytes;
  SFU_BoolTypeDef is_protection_to_be_applied = SFU_FALSE;
  SFU_ErrorStatus e_ret_status = SFU_SUCCESS;

  /* Unlock the Flash to enable the flash control register access *************/
  (void) HAL_FLASH_Unlock();

  /* Clear error flags raised during previous operation */
  __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_WRPERR | FLASH_FLAG_PGERR);

  /* Unlock the Options Bytes *************************************************/
  (void) HAL_FLASH_OB_Unlock();

  /* Get the current Option Bytes configuration ******************************/
  HAL_FLASHEx_OBGetConfig(&flash_option_bytes);

  /* Flash configuration check point: the flow-control chain contains an
     UNCONDITIONAL FLOW_STEP_UBE (see sfu_boot.h) that the reference port
     performs in its flash-configuration helpers. It MUST be stepped here in
     every branch or FLOW_CONTROL_CHECK(FLOW_CTRL_STATIC_PROTECT) aborts the
     boot with a security error. */
  FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_UBE, FLOW_CTRL_UBE);

  /* Check/Apply RDP level 1. If RDP level 2 is already applied the option
     bytes cannot be modified anymore. */
  if (flash_option_bytes.RDPLevel == OB_RDP_LEVEL_2)
  {
#ifdef SFU_WRP_PROTECT_ENABLE
    if (SFU_LL_SECU_CheckProtectionWRP(&flash_option_bytes) != SFU_SUCCESS)
    {
      TRACE("\r\n= [SBOOT] System Security Configuration failed! Product blocked.");
      /* Security issue : execution stopped ! */
      SFU_EXCPT_Security_Error();
    }
#endif /* SFU_WRP_PROTECT_ENABLE */

    /* RDP level 2 ==> flow control by-passed */
    FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_RDP, FLOW_CTRL_RDP);
  }
  else
  {
    /* Check/Apply WRP ********************************************************/
#ifdef SFU_WRP_PROTECT_ENABLE
    if (e_ret_status == SFU_SUCCESS)
    {
      e_ret_status = SFU_LL_SECU_SetProtectionWRP(&flash_option_bytes, &is_protection_to_be_applied);
    }
#endif /* SFU_WRP_PROTECT_ENABLE */

    /* Check/Apply RDP : should be done as last option bytes configuration */
#ifdef SFU_RDP_PROTECT_ENABLE
    if (e_ret_status == SFU_SUCCESS)
    {
      e_ret_status = SFU_LL_SECU_SetProtectionRDP(&flash_option_bytes, &is_protection_to_be_applied);
    }
#endif  /* SFU_RDP_PROTECT_ENABLE */

    if (e_ret_status == SFU_SUCCESS)
    {
      if (is_protection_to_be_applied != SFU_FALSE)
      {
        /* Generate System Reset to reload the new option byte values *********/
        /* WARNING: if a protection can't be set, this creates a reset loop! */
        (void) HAL_FLASH_OB_Launch();
      }
    }
  }

  /* Lock the Options Bytes ***************************************************/
  (void) HAL_FLASH_OB_Lock();

  /* Lock the Flash to disable the flash control register access (recommended
  to protect the FLASH memory against possible unwanted operation) *********/
  (void) HAL_FLASH_Lock();

  return e_ret_status;
}

/**
  * @brief  Apply Runtime security protections.
  *         Runtime security protections have to be re-configured at each Reset.
  * @param  uStep Configuration step : SFU_INITIAL_CONFIGURATION,
  *         SFU_SECOND_CONFIGURATION, SFU_THIRD_CONFIGURATION
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
SFU_ErrorStatus SFU_LL_SECU_CheckApplyRuntimeProtections(uint8_t uStep)
{
  SFU_ErrorStatus e_ret_status = SFU_SUCCESS;
  SFU_ProtectionTypeDef runtime_protection = SFU_PROTECTIONS_NONE;

  UNUSED(uStep);

  /* The F072 development configuration applies no runtime protection:
     no MPU (absent on Cortex-M0), tamper/DAP/DMA/IWDG switches are off.
     With every FLOW_STEP_* equal to zero the flow-control chain stays
     consistent without explicit steps here. */

#ifdef SFU_IWDG_PROTECT_ENABLE
  if (uStep == SFU_INITIAL_CONFIGURATION)
  {
    if (SFU_LL_SECU_SetProtectionIWDG() == SFU_SUCCESS)
    {
      runtime_protection |= SFU_RUNTIME_PROTECTION_IWDG;
      FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_IWDG, FLOW_CTRL_IWDG);
    }
    else
    {
      e_ret_status = SFU_ERROR;
    }
  }
  else
  {
    FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_IWDG, FLOW_CTRL_IWDG);
  }
#endif /* SFU_IWDG_PROTECT_ENABLE */

#ifdef SFU_DMA_PROTECT_ENABLE
  if (SFU_LL_SECU_SetProtectionDMA() == SFU_SUCCESS)
  {
    runtime_protection |= SFU_RUNTIME_PROTECTION_DMA;
    FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_DMA, FLOW_CTRL_DMA);
  }
  else
  {
    e_ret_status = SFU_ERROR;
  }
#endif /* SFU_DMA_PROTECT_ENABLE */

#if defined(SFU_VERBOSE_DEBUG_MODE)
  TRACE("\r\n= [SBOOT] RuntimeProtections: %x", runtime_protection);
#else
  UNUSED(runtime_protection);
#endif /* SFU_VERBOSE_DEBUG_MODE */

  return e_ret_status;
}

/**
  * @brief  Return the reset source detected after a reboot.
  * @param  peResetpSourceId: to be filled with the detected source of reset
  * @note   In case of multiple reset sources this function returns the first identified one.
  * @retval None.
  */
void SFU_LL_SECU_GetResetSources(SFU_RESET_IdTypeDef *peResetpSourceId)
{
  /* Check the reset flags in decreasing priority order */
  if ((__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) || (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET))
  {
    *peResetpSourceId = SFU_RESET_WDG_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET)
  {
    *peResetpSourceId = SFU_RESET_LOW_POWER;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)
  {
    *peResetpSourceId = SFU_RESET_SW_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_OBLRST) != RESET)
  {
    *peResetpSourceId = SFU_RESET_OB_LOADER;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET)
  {
    *peResetpSourceId = SFU_RESET_BOR_RESET;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)
  {
    *peResetpSourceId = SFU_RESET_HW_RESET;
  }
  else
  {
    *peResetpSourceId = SFU_RESET_UNKNOWN;
  }
}

/**
  * @brief  Clear the reset sources. This function should be called after the
  *         reset sources have been processed.
  * @param  None.
  * @retval None.
  */
void SFU_LL_SECU_ClearResetSources(void)
{
  __HAL_RCC_CLEAR_RESET_FLAGS();
}

/**
  * @brief  Refresh Watchdog : reload counter.
  * @param  None.
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
SFU_ErrorStatus SFU_LL_SECU_IWDG_Refresh(void)
{
#ifdef SFU_IWDG_PROTECT_ENABLE
  if (HAL_IWDG_Refresh(&IwdgHandle) == HAL_OK)
  {
    return SFU_SUCCESS;
  }
  return SFU_ERROR;
#else
  /* The IWDG is ARMED by main.c before the secure boot service, but SBSFU's
   * periodic refresh hooks land here — refresh the main.c watchdog for real,
   * otherwise the 26 s timeout can expire across the software ECDSA verify
   * plus the application's own start-up window. */
  extern IWDG_HandleTypeDef hiwdg;
  (void)HAL_IWDG_Refresh(&hiwdg);
  return SFU_SUCCESS;
#endif /* SFU_IWDG_PROTECT_ENABLE */
}

#ifdef SFU_WRP_PROTECT_ENABLE
/**
  * @brief  Build the F0 WRP bit mask covering the bootloader pages.
  *         On the STM32F072 each WRP bit protects one 4K sector (2 pages).
  * @retval 32-bit WRP page mask (HAL OB_WRP_* encoding)
  */
static uint32_t SFU_LL_SECU_GetWRPMask(void)
{
  uint32_t mask = 0U;
  uint32_t sector;

  for (sector = (SFU_PROTECT_WRP_PAGE_START_1 / 2U); sector <= (SFU_PROTECT_WRP_PAGE_END_1 / 2U); sector++)
  {
    mask |= (1UL << sector);
  }
  return mask;
}

/**
  * @brief  Check the WRP protection of the bootloader area.
  * @param  psFlashOptionBytes: pointer to the Option Bytes structure
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
static SFU_ErrorStatus SFU_LL_SECU_CheckProtectionWRP(FLASH_OBProgramInitTypeDef *psFlashOptionBytes)
{
  uint32_t mask = SFU_LL_SECU_GetWRPMask();

  /* On F0, OBGetConfig returns the WRP register: a SET bit means NOT protected */
  if ((~(psFlashOptionBytes->WRPPage) & mask) != mask)
  {
    return SFU_ERROR;
  }
  FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_WRP, FLOW_CTRL_WRP);
  return SFU_SUCCESS;
}

/**
  * @brief  Apply the WRP protection of the bootloader area if not yet applied.
  * @param  psFlashOptionBytes: pointer to the Option Bytes structure
  * @param  pbIsProtectionToBeApplied: set to SFU_TRUE if an OB launch is needed
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
static SFU_ErrorStatus SFU_LL_SECU_SetProtectionWRP(FLASH_OBProgramInitTypeDef *psFlashOptionBytes,
                                                    SFU_BoolTypeDef *pbIsProtectionToBeApplied)
{
  SFU_ErrorStatus e_ret_status = SFU_SUCCESS;
  uint32_t mask = SFU_LL_SECU_GetWRPMask();

  if ((~(psFlashOptionBytes->WRPPage) & mask) != mask)
  {
    psFlashOptionBytes->OptionType = OPTIONBYTE_WRP;
    psFlashOptionBytes->WRPState   = OB_WRPSTATE_ENABLE;
    psFlashOptionBytes->WRPPage    = mask;

    if (HAL_FLASHEx_OBProgram(psFlashOptionBytes) == HAL_OK)
    {
      *pbIsProtectionToBeApplied = SFU_TRUE;
      FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_WRP, FLOW_CTRL_WRP);
    }
    else
    {
      e_ret_status = SFU_ERROR;
    }
  }
  else
  {
    FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_WRP, FLOW_CTRL_WRP);
  }
  return e_ret_status;
}
#endif /* SFU_WRP_PROTECT_ENABLE */

#ifdef SFU_RDP_PROTECT_ENABLE
/**
  * @brief  Apply the RDP protection if not yet applied.
  * @param  psFlashOptionBytes: pointer to the Option Bytes structure
  * @param  pbIsProtectionToBeApplied: set to SFU_TRUE if an OB launch is needed
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
static SFU_ErrorStatus SFU_LL_SECU_SetProtectionRDP(FLASH_OBProgramInitTypeDef *psFlashOptionBytes,
                                                    SFU_BoolTypeDef *pbIsProtectionToBeApplied)
{
  SFU_ErrorStatus e_ret_status = SFU_SUCCESS;

  if (psFlashOptionBytes->RDPLevel != SFU_PROTECT_RDP_LEVEL)
  {
    psFlashOptionBytes->OptionType = OPTIONBYTE_RDP;
    psFlashOptionBytes->RDPLevel   = SFU_PROTECT_RDP_LEVEL;

    if (HAL_FLASHEx_OBProgram(psFlashOptionBytes) == HAL_OK)
    {
      *pbIsProtectionToBeApplied = SFU_TRUE;
      FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_RDP, FLOW_CTRL_RDP);
    }
    else
    {
      e_ret_status = SFU_ERROR;
    }
  }
  else
  {
    FLOW_CONTROL_STEP(uFlowProtectValue, FLOW_STEP_RDP, FLOW_CTRL_RDP);
  }
  return e_ret_status;
}
#endif /* SFU_RDP_PROTECT_ENABLE */

#ifdef SFU_DMA_PROTECT_ENABLE
/**
  * @brief  Disable the DMA controllers so a stale DMA configuration cannot
  *         touch the bootloader while it runs.
  * @param  None.
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
SFU_ErrorStatus SFU_LL_SECU_SetProtectionDMA(void)
{
  __HAL_RCC_DMA1_CLK_DISABLE();
#if defined(DMA2)
  __HAL_RCC_DMA2_CLK_DISABLE();
#endif /* DMA2 */
  return SFU_SUCCESS;
}
#endif /* SFU_DMA_PROTECT_ENABLE */

#ifdef SFU_IWDG_PROTECT_ENABLE
/**
  * @brief  Enable the independent watchdog.
  * @param  None.
  * @retval SFU_ErrorStatus SFU_SUCCESS if successful, SFU_ERROR otherwise.
  */
static SFU_ErrorStatus SFU_LL_SECU_SetProtectionIWDG(void)
{
  /* LSI ~40 kHz / 256 => max reload 4095 gives ~26 s */
  IwdgHandle.Instance       = IWDG;
  IwdgHandle.Init.Prescaler = IWDG_PRESCALER_256;
  IwdgHandle.Init.Reload    = 4095U;
  IwdgHandle.Init.Window    = IWDG_WINDOW_DISABLE;

  if (HAL_IWDG_Init(&IwdgHandle) == HAL_OK)
  {
    return SFU_SUCCESS;
  }
  return SFU_ERROR;
}
#endif /* SFU_IWDG_PROTECT_ENABLE */
