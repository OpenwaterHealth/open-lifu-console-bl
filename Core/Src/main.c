/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : open-lifu console SBSFU secure bootloader (STM32F072)
  *
  * Boot flow (modeled on the open-motion-console-bl H743 reference):
  *   1. Clock + backup-domain bring-up, app<->bootloader contract in the RTC
  *      backup registers (BKP0R 'OWBL' signature, BKP1R 'DFU!' force-DFU
  *      request, BKP2R boot-fail counter — same contract as the legacy
  *      bootloader, so the existing application works unmodified).
  *   2. Unless DFU is forced, run the SBSFU secure boot service: verify the
  *      ECDSA-signed header + SHA-256 firmware tag of the image in the active
  *      slot and launch it (SFU_BOOT_RunSecureBootService does not return on
  *      success).
  *   3. If no valid firmware exists (or DFU was requested / the boot-fail
  *      counter tripped), fall through to USB DFU download mode.
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "usb_device.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "memory_map.h"
#include "usbd_dfu_if.h"
#include "sfu_boot.h"
#include "sfu_trace.h"
#include "version.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
IWDG_HandleTypeDef hiwdg;

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_IWDG_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/**
  * @brief  Enable write access to the RTC backup registers.
  *         Proven sequence from the legacy lifu bootloader: PWR clock + DBP,
  *         then make sure the RTC domain is clocked (LSI was switched on in
  *         SystemClock_Config) so the BKPxR registers are accessible.
  */
static void bl_bkp_enable(void)
{
  __HAL_RCC_PWR_CLK_ENABLE();
  HAL_PWR_EnableBkUpAccess();

  if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0U)
  {
    uint32_t rtcsel = (RCC->BDCR & RCC_BDCR_RTCSEL);
    if (rtcsel == 0U)
    {
      /* Select LSI for RTC if no source configured (RTCSEL=10b on STM32F0) */
      MODIFY_REG(RCC->BDCR, RCC_BDCR_RTCSEL, RCC_BDCR_RTCSEL_1);
    }
    SET_BIT(RCC->BDCR, RCC_BDCR_RTCEN);
  }
}

/**
  * @brief  Initialise the persistent boot state on first power-up.
  */
static void bl_bootstate_init(void)
{
  bl_bkp_enable();

  if (RTC->BKP0R != BL_BKP_SIGNATURE)
  {
    RTC->BKP0R = BL_BKP_SIGNATURE;
    RTC->BKP1R = 0U; /* DFU request */
    RTC->BKP2R = 0U; /* state + fail counter */
    RTC->BKP3R = 0U; /* legacy: last bad fw marker */
    RTC->BKP4R = 0U; /* legacy: auth cache (unused by SBSFU) */
  }
}

/**
  * @brief  Turn all status LEDs off (active-low).
  */
static void bl_leds_all_off(void)
{
  HAL_GPIO_WritePin(LD_R_GPIO_Port, LD_R_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_G_GPIO_Port, LD_G_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_B_GPIO_Port, LD_B_Pin, GPIO_PIN_SET);
}

/**
  * @brief  Board preparation for USB DFU mode: release the USB mux/reset line
  *         (PA15 high) so the device port is connected, and show the blue LED.
  */
static void bl_prepare_usb_dfu_mode(void)
{
  bl_leds_all_off();
  HAL_GPIO_WritePin(USB_RESET_GPIO_Port, USB_RESET_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(LD_B_GPIO_Port, LD_B_Pin, GPIO_PIN_RESET);
}

/**
  * @brief  Jump to the STM32 built-in (system memory) DFU bootloader.
  * @note   Requested by the application writing BL_BKP_REQ_STM32_DFU_MAGIC
  *         ('DFUS') to BKP1R. On STM32F072 the system bootloader lives at
  *         0x1FFFC800. The Cortex-M0 has no VTOR, so system memory is remapped
  *         to 0x00000000 (SYSCFG MEM_MODE) before the jump. Does not return.
  * @note   The board USB device port is gated by the USB mux/reset line, which
  *         the ROM bootloader knows nothing about, so it is released here first.
  *         GPIO output latches hold their state across the RCC de-init below.
  * @note   The ROM bootloader will not refresh our IWDG, so it must not be
  *         running here. Guaranteed by the caller: it jumps before MX_IWDG_Init()
  *         (this boot never arms it) and the app's IWDG does not survive its
  *         software reset into us (verified on hardware). See the caller for the
  *         full rationale.
  */
static void bl_jump_to_system_bootloader(void)
{
  const uint32_t sysmem_base = 0x1FFFC800U;   /* STM32F07x system memory */
  uint32_t boot_sp;
  uint32_t boot_pc;

  /* Release the USB device-port mux (PA15 high) so the ROM bootloader can
   * enumerate; blue LED on for a visual cue. */
  MX_GPIO_Init();
  bl_prepare_usb_dfu_mode();

  __disable_irq();

  /* Reset the USB peripheral so the ROM bootloader sees it in a clean state
   * (the application had it enumerated as a CDC device before it reset us). */
  __HAL_RCC_USB_FORCE_RESET();
  __HAL_RCC_USB_RELEASE_RESET();

  /* Return the clock tree to its reset state (HSI 8 MHz). The ROM bootloader
   * sets up its own clocks, including HSI48 for USB. */
  HAL_RCC_DeInit();

  /* Silence SysTick and clear any pending/enabled IRQs. */
  SysTick->CTRL = 0U;
  SysTick->LOAD = 0U;
  SysTick->VAL  = 0U;
  NVIC->ICER[0] = 0xFFFFFFFFU;
  NVIC->ICPR[0] = 0xFFFFFFFFU;

  /* Cortex-M0 has no VTOR: map system flash to 0x00000000 so the ROM
   * bootloader's vector table is used (SYSCFG MEM_MODE = 01b). */
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  SYSCFG->CFGR1 = (SYSCFG->CFGR1 & ~SYSCFG_CFGR1_MEM_MODE) | SYSCFG_CFGR1_MEM_MODE_0;

  /* Load the ROM bootloader's initial stack pointer and reset vector, jump.
   * Re-enable interrupts first: the ROM bootloader is entered by a call (not a
   * real reset), so it inherits our PRIMASK — and its USB DFU is interrupt
   * driven, so it will not enumerate with interrupts masked. All pending IRQs
   * were just cleared above, so nothing spurious can fire before the jump. */
  boot_sp = *(volatile uint32_t *)(sysmem_base);
  boot_pc = *(volatile uint32_t *)(sysmem_base + 4U);
  __set_MSP(boot_sp);
  __enable_irq();
  ((void (*)(void))boot_pc)();

  while (1)
  {
    /* never reached */
  }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  uint32_t force_dfu = 0U;
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* Persistent boot state lives in the RTC backup registers — the contract is
   * shared with the application (see memory_map.h). */
  bl_bootstate_init();

  /* App-requested STM32 system (ROM) bootloader: the application writes 'DFUS'
   * to BKP1R and resets. Jump immediately — before the IWDG is armed and before
   * any secure-boot work. One-shot: consume the request so it cannot loop.
   *
   * Watchdog safety: the ROM bootloader does not know about (and will not
   * refresh) our IWDG, so it must NOT be running during a ST DFU session or the
   * ~26 s timeout would reset the device mid-update. Two guarantees keep it off:
   *   1. This jump runs BEFORE MX_IWDG_Init() below, so this boot never arms it
   *      (do not move MX_IWDG_Init() above this check).
   *   2. The app's IWDG does not survive its NVIC_SystemReset() into us — a
   *      software system reset clears the IWDG on this F072 (verified on
   *      hardware: the device sits in ROM DFU indefinitely, >>26 s).
   * A unit built with the hardware-watchdog option byte (IWDG_SW=0) would auto-
   * start the IWDG at reset and cap ST DFU at ~26 s; these units use IWDG_SW=1. */
  if (RTC->BKP1R == BL_BKP_REQ_STM32_DFU_MAGIC)
  {
    RTC->BKP1R = 0U;
    bl_jump_to_system_bootloader(); /* does not return */
  }

  /* App-requested DFU: the application writes 'DFU!' to BKP1R and resets.
   * One-shot: the request is consumed here so it cannot loop into DFU. */
  if (RTC->BKP1R == BL_BKP_REQ_DFU_MAGIC)
  {
    RTC->BKP1R = 0U;
    force_dfu = 1U;
  }

  /* Failsafe: count boot attempts in BKP2R (bits 8-15). The application clears
   * the counter once it is up and running; if BL_BOOT_FAIL_MAX launches never
   * made it that far, stop retrying and fall into DFU so the unit is always
   * recoverable in the field. The counter is cleared again when a new image is
   * downloaded (usbd_dfu_if.c MEM_If_Erase_FS). */
  if (force_dfu == 0U)
  {
    uint32_t state    = RTC->BKP2R;
    uint32_t boot_cnt = (state & BL_BKP_STATE_FAILCOUNT_MASK) >> BL_BKP_STATE_FAILCOUNT_SHIFT;
    if (boot_cnt >= BL_BOOT_FAIL_MAX)
    {
      force_dfu = 1U;
    }
    else
    {
      /* record this boot attempt before the jump */
      state &= ~BL_BKP_STATE_FAILCOUNT_MASK;
      state |= ((boot_cnt + 1U) << BL_BKP_STATE_FAILCOUNT_SHIFT) & BL_BKP_STATE_FAILCOUNT_MASK;
      RTC->BKP2R = state;
    }
  }

  /* Arm the watchdog before the secure boot service: LSI/256 with max reload
   * gives ~26 s, enough headroom for the software ECDSA verify on the M0 while
   * still recovering from a hung verify or DFU session. Refreshed in the DFU
   * loop below; the launched application must refresh or re-init it.
   * NOTE: matches the legacy bootloader behaviour of always running the IWDG.
   * NOTE: must stay AFTER the BL_BKP_REQ_STM32_DFU_MAGIC jump above — the STM32
   * ROM bootloader cannot refresh this watchdog (see that block). */
  MX_IWDG_Init();

  /* USER CODE END SysInit */

  /* USER CODE BEGIN 2 */

  if (force_dfu == 0U)
  {
    /* Launches the firmware in the active slot if its ECDSA header signature
     * and SHA-256 firmware tag verify — in that case this call never returns.
     * It returns only when no valid firmware is available (or the image is
     * corrupted/unsigned): fall through to USB DFU download mode. */
    (void)SFU_BOOT_RunSecureBootService();
  }
  else
  {
    /* Forced DFU (app 'DFU!' request or boot-fail fallback) bypasses the secure
     * boot service, which is what brings up the trace UART and prints the boot
     * banner. Print it here so the DFU path is not silent on the trace UART. */
    SFU_BOOT_PrintForcedDfuBanner();
  }

  /* -------------------- USB DFU download mode -------------------- */

  MX_GPIO_Init();
  bl_prepare_usb_dfu_mode();
  MX_USB_DEVICE_Init();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    HAL_IWDG_Refresh(&hiwdg);

    if (DFU_ImageDownloadComplete() != 0U)
    {
      if (DFU_IsRollback() != 0U)
      {
        /* Version downgrade: destroy the downloaded image so it can never boot
         * and stay in DFU for a valid upload. */
        DFU_InvalidateImage();
        DFU_ClearDownloadState();
      }
      else
      {
        /* Give the host a moment to close the DFU session cleanly, then reboot
         * into the freshly downloaded image (verified by SBSFU at startup). */
        HAL_Delay(50);
        NVIC_SystemReset();
      }
    }

    /* Host-requested reset (DNLOAD to the virtual reset address): the clean
     * way for a host tool to leave DFU without flashing — e.g. the SDK
     * aborting an update after its pre-flight downgrade check. SBSFU fully
     * re-verifies the slot on the way back up, so if the slot is intact the
     * application boots; if not, we simply return to DFU. */
    if (DFU_ResetRequested() != 0U)
    {
      HAL_Delay(50);
      NVIC_SystemReset();
    }

    /* Blink the blue LED while waiting for a download */
    HAL_GPIO_TogglePin(LD_B_GPIO_Port, LD_B_Pin);
    HAL_Delay(500);

    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  *        HSI (8 MHz) x PLL6 = 48 MHz SYSCLK; HSI48 feeds the USB peripheral;
  *        LSI on for the IWDG and the RTC backup-register domain.
  *        (Same configuration as the legacy lifu bootloader.)
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_HSI48
                              |RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL6;
  RCC_OscInitStruct.PLL.PREDIV = RCC_PREDIV_DIV1;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK)
  {
    Error_Handler();
  }

  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USB;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_HSI48;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief IWDG Initialization Function
  *        LSI (~40 kHz) / 256 prescaler, max reload 4095 -> ~26 s timeout.
  * @retval None
  */
static void MX_IWDG_Init(void)
{
  hiwdg.Instance = IWDG;
  hiwdg.Init.Prescaler = IWDG_PRESCALER_256;
  hiwdg.Init.Window = IWDG_WINDOW_DISABLE;
  hiwdg.Init.Reload = 4095U;
  if (HAL_IWDG_Init(&hiwdg) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief GPIO Initialization Function
  *        Board GPIO bring-up for DFU mode (CubeMX-generated pin map from the
  *        legacy bootloader: LEDs, USB reset, power good inputs, HV safe state).
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, V12_ENABLE_Pin|SCL_CFG_Pin|SDA_REM_Pin|LD_G_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, USB_RESET_Pin|ADC_PD_Pin|HB_LED_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SYS_RDY_GPIO_Port, SYS_RDY_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, HV_SHUTDOWN_Pin|HV_ON_Pin|LD_B_Pin|LD_R_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : V12_ENABLE_Pin HV_SHUTDOWN_Pin HV_ON_Pin SCL_CFG_Pin
                           SDA_REM_Pin LD_B_Pin LD_R_Pin LD_G_Pin */
  GPIO_InitStruct.Pin = V12_ENABLE_Pin|HV_SHUTDOWN_Pin|HV_ON_Pin|SCL_CFG_Pin
                          |SDA_REM_Pin|LD_B_Pin|LD_R_Pin|LD_G_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : V12_PWR_G_Pin V5B_PWR_G_Pin V5A_PWR_G_Pin PWR_G_Pin */
  GPIO_InitStruct.Pin = V12_PWR_G_Pin|V5B_PWR_G_Pin|V5A_PWR_G_Pin|PWR_G_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : USB_RESET_Pin ADC_PD_Pin HB_LED_Pin */
  GPIO_InitStruct.Pin = USB_RESET_Pin|ADC_PD_Pin|HB_LED_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SYS_RDY_Pin */
  GPIO_InitStruct.Pin = SYS_RDY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SYS_RDY_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  UNUSED(file);
  UNUSED(line);
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
