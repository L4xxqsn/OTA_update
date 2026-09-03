/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32l1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define VCC_DRV_Pin GPIO_PIN_13
#define VCC_DRV_GPIO_Port GPIOC
#define STATUS_4G_Pin GPIO_PIN_15
#define STATUS_4G_GPIO_Port GPIOC
#define PWRKEY_Pin GPIO_PIN_1
#define PWRKEY_GPIO_Port GPIOH
#define RI_Pin GPIO_PIN_0
#define RI_GPIO_Port GPIOC
#define RESET_N_Pin GPIO_PIN_2
#define RESET_N_GPIO_Port GPIOC
#define TXD_4G_Pin GPIO_PIN_2
#define TXD_4G_GPIO_Port GPIOA
#define RXD_4G_Pin GPIO_PIN_3
#define RXD_4G_GPIO_Port GPIOA
#define OE_TXS0108_Pin GPIO_PIN_4
#define OE_TXS0108_GPIO_Port GPIOA
#define CTL_1V8_Pin GPIO_PIN_1
#define CTL_1V8_GPIO_Port GPIOB
#define DTR_Pin GPIO_PIN_2
#define DTR_GPIO_Port GPIOB
#define FLASH_HOLD_Pin GPIO_PIN_10
#define FLASH_HOLD_GPIO_Port GPIOB
#define FLASH_NSS_Pin GPIO_PIN_11
#define FLASH_NSS_GPIO_Port GPIOB
#define CTL_4V_Pin GPIO_PIN_13
#define CTL_4V_GPIO_Port GPIOB
#define CTL_4VOE_Pin GPIO_PIN_14
#define CTL_4VOE_GPIO_Port GPIOB
#define Battery_Vol_Pin GPIO_PIN_15
#define Battery_Vol_GPIO_Port GPIOB
#define Battery_CHK_Pin GPIO_PIN_8
#define Battery_CHK_GPIO_Port GPIOC
#define BLE_TXD_Pin GPIO_PIN_9
#define BLE_TXD_GPIO_Port GPIOA
#define BLE_RXD_Pin GPIO_PIN_10
#define BLE_RXD_GPIO_Port GPIOA
#define LED_Pin GPIO_PIN_12
#define LED_GPIO_Port GPIOC
#define BLE_INT_Pin GPIO_PIN_3
#define BLE_INT_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
