/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f0xx_hal_msp.c
  * @brief   MSP Initialization and de-Initialization codes.
  *          SBSFU bootloader: USART3 (PC10/PC11, AF1) is the SFU trace UART —
  *          plain blocking TX, no DMA. The CRC is clocked for the SBSFU / SE
  *          integrity helpers. The legacy TIM/DMA MSP code is gone with the
  *          peripherals themselves (SysTick is the HAL timebase now).
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
#include "main.h"

/**
  * Initializes the Global MSP.
  */
void HAL_MspInit(void)
{
  __HAL_RCC_SYSCFG_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
}

/**
  * @brief CRC MSP Initialization
  * @param hcrc: CRC handle pointer
  * @retval None
  */
void HAL_CRC_MspInit(CRC_HandleTypeDef* hcrc)
{
  if (hcrc->Instance == CRC)
  {
    __HAL_RCC_CRC_CLK_ENABLE();
  }
}

/**
  * @brief CRC MSP De-Initialization
  * @param hcrc: CRC handle pointer
  * @retval None
  */
void HAL_CRC_MspDeInit(CRC_HandleTypeDef* hcrc)
{
  if (hcrc->Instance == CRC)
  {
    __HAL_RCC_CRC_CLK_DISABLE();
  }
}

/**
  * @brief UART MSP Initialization
  *        USART3 trace UART on PC10 (TX) / PC11 (RX), AF1 — same pins as the
  *        legacy bootloader's debug console, blocking mode (no DMA).
  * @param huart: UART handle pointer
  * @retval None
  */
void HAL_UART_MspInit(UART_HandleTypeDef* huart)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};

  if (huart->Instance == USART3)
  {
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /**USART3 GPIO Configuration
    PC10     ------> USART3_TX
    PC11     ------> USART3_RX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_10 | GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF1_USART3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
  }
}

/**
  * @brief UART MSP De-Initialization
  * @param huart: UART handle pointer
  * @retval None
  */
void HAL_UART_MspDeInit(UART_HandleTypeDef* huart)
{
  if (huart->Instance == USART3)
  {
    __HAL_RCC_USART3_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_10 | GPIO_PIN_11);
  }
}
