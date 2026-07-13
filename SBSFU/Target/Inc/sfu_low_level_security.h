/**
  ******************************************************************************
  * @file    sfu_low_level_security.h
  * @author  MCD Application Team
  * @brief   This file contains definitions for Secure Firmware Update security
  *          low level interface — STM32F072 port.
  *
  *          The F072 (Cortex-M0) has NO MPU, NO PCROP and NO secure-user
  *          memory. The available hardware protections are WRP, RDP, IWDG
  *          and anti-tamper. The bootloader currently runs with
  *          SECBOOT_DISABLE_SECURITY_IPS (development mode) so none of them
  *          are applied; WRP/RDP hooks are implemented for the future
  *          production option-byte pass, the absent IPs raise a compile
  *          error if their switches are ever enabled.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef SFU_LOW_LEVEL_SECURITY_H
#define SFU_LOW_LEVEL_SECURITY_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "sfu_fwimg_regions.h"
#include "sfu_def.h"

/* Unsupported IPs on STM32F072: fail loudly if someone enables them */
#if defined(SFU_MPU_PROTECT_ENABLE)
#error "The STM32F072 (Cortex-M0) has no MPU: SFU_MPU_PROTECT_ENABLE cannot be used on this target."
#endif
#if defined(SFU_PCROP_PROTECT_ENABLE)
#error "The STM32F072 has no PCROP: SFU_PCROP_PROTECT_ENABLE cannot be used on this target."
#endif
#if defined(SFU_SECURE_USER_PROTECT_ENABLE)
#error "The STM32F072 has no secure user memory: SFU_SECURE_USER_PROTECT_ENABLE cannot be used on this target."
#endif
#if defined(SFU_FWALL_PROTECT_ENABLE)
#error "The STM32F072 has no Firewall: SFU_FWALL_PROTECT_ENABLE cannot be used on this target."
#endif

/* Exported constants --------------------------------------------------------*/

/*!< First page of the WRP area protecting the bootloader (0 based) */
#define SFU_PROTECT_WRP_PAGE_START_1    ((uint32_t)((SFU_BOOT_BASE_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE))

/*!< Last page of the WRP area protecting the bootloader */
#define SFU_PROTECT_WRP_PAGE_END_1      ((uint32_t)((SFU_ROM_ADDR_END - FLASH_BASE) / FLASH_PAGE_SIZE))

/* Exported types ------------------------------------------------------------*/
/**
  * @brief  SFU Reset source identification
  */
typedef enum
{
  SFU_RESET_UNKNOWN = 0x00U,
  SFU_RESET_WDG_RESET,
  SFU_RESET_LOW_POWER,
  SFU_RESET_HW_RESET,
  SFU_RESET_BOR_RESET,
  SFU_RESET_SW_RESET,
  SFU_RESET_OB_LOADER,
} SFU_RESET_IdTypeDef;

/**
  * @brief  SFU protections runtime bitmask
  */
typedef uint32_t SFU_ProtectionTypeDef;

#define SFU_PROTECTIONS_NONE                 ((SFU_ProtectionTypeDef)0x00000000U)
#define SFU_RUNTIME_PROTECTION_MPU           ((SFU_ProtectionTypeDef)0x00000001U)
#define SFU_RUNTIME_PROTECTION_IWDG          ((SFU_ProtectionTypeDef)0x00000002U)
#define SFU_RUNTIME_PROTECTION_DAP           ((SFU_ProtectionTypeDef)0x00000004U)
#define SFU_RUNTIME_PROTECTION_DMA           ((SFU_ProtectionTypeDef)0x00000008U)
#define SFU_RUNTIME_PROTECTION_ANTI_TAMPER   ((SFU_ProtectionTypeDef)0x00000010U)
#define SFU_RUNTIME_PROTECTION_CLOCK_MONITOR ((SFU_ProtectionTypeDef)0x00000020U)
#define SFU_RUNTIME_PROTECTION_TEMP_MONITOR  ((SFU_ProtectionTypeDef)0x00000040U)

/**
  * @brief  SFU protection configuration steps
  */
#define SFU_INITIAL_CONFIGURATION            (0x00U)  /*!< First configuration */
#define SFU_SECOND_CONFIGURATION             (0x01U)  /*!< Second configuration */
#define SFU_THIRD_CONFIGURATION              (0x02U)  /*!< Third configuration */

/* Exported functions ------------------------------------------------------- */
SFU_ErrorStatus SFU_LL_SECU_IWDG_Refresh(void);
SFU_ErrorStatus SFU_LL_SECU_CheckApplyStaticProtections(void);
SFU_ErrorStatus SFU_LL_SECU_CheckApplyRuntimeProtections(uint8_t uStep);
void            SFU_LL_SECU_GetResetSources(SFU_RESET_IdTypeDef *peResetpSourceId);
void            SFU_LL_SECU_ClearResetSources(void);
#ifdef SFU_DAP_PROTECT_ENABLE
SFU_ErrorStatus SFU_LL_SECU_SetProtectionDAP(void);
#endif /* SFU_DAP_PROTECT_ENABLE */
#ifdef SFU_DMA_PROTECT_ENABLE
SFU_ErrorStatus SFU_LL_SECU_SetProtectionDMA(void);
#endif /* SFU_DMA_PROTECT_ENABLE */
#ifdef SFU_TAMPER_PROTECT_ENABLE
SFU_ErrorStatus SFU_LL_SECU_SetProtectionANTI_TAMPER(void);
#endif /* SFU_TAMPER_PROTECT_ENABLE */

#ifdef __cplusplus
}
#endif

#endif /* SFU_LOW_LEVEL_SECURITY_H */
