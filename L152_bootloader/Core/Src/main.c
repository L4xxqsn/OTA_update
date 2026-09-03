/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stdio.h"
#include "string.h"
#include "stdbool.h"
//#include "flash_test.h"
#include "flash_io.h"
#include "bootloader_app.h"

#define APP_MAX_SIZE    0x15FFF    // APP max 87KB

// Internal flash: bootloader address range
#define FLASH_INT_START 0x08000000
#define FLASH_INT_END   0x08008000

// Internal flash: message area address range
#define FLASH_MSG_START 0x08008100
#define FLASH_MSG_END   0x08009000

// Internal flash: APP firmware address range
#define FLASH_APP_START 0x0800A000
#define FLASH_APP_END   0x0801FFFF


// External flash: W25Q16 logical address range
#define FLASH_EXT_START 0x000000
#define FLASH_EXT_END   0x1FFFFF


uint32_t CRC32_Calc(const uint8_t *data, uint32_t len);
extern void hal_app_jump(uint32_t u32AppAddr);
void init_turn();


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

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_USART3_UART_Init();
  /* USER CODE BEGIN 2 */

  for(int i = 0; i < 3; i++)
  {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_SET);
    HAL_Delay(100);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_12, GPIO_PIN_RESET);
    HAL_Delay(100);
  }

  // 应用层写入 12 字节：[file_len(4B)][file_crc(4B)][0x55AA55AA(4B)]
  uint32_t msg[3] = {0};  // msg[0]=file_len, msg[1]=file_crc, msg[2]=flag
  IF_Read(FLASH_MSG_START, (uint8_t *)msg, sizeof(msg));

  if (msg[2] == 0x55AA55AA && msg[0] > 0 && msg[0] <= (FLASH_APP_END - FLASH_APP_START))
  {
      uint32_t file_len = msg[0];
      uint32_t file_crc = msg[1];

      //  外置 Flash 固件整体 CRC32 校验

      uint32_t crc = 0xFFFFFFFF;
      uint8_t  buf[256];
      uint32_t remain = file_len;
      uint32_t addr   = FLASH_EXT_START;

      while (remain > 0) {
          uint16_t chunk = (remain > sizeof(buf)) ? (uint16_t)sizeof(buf) : (uint16_t)remain;
          sFLASH_ReadBuffer(buf, addr, chunk);   // 从外置 Flash 读取数据块

          // 逐字节计算 CRC
          for (uint16_t k = 0; k < chunk; k++) {
              crc ^= buf[k];
              for (uint8_t j = 0; j < 8; j++) {
                  if (crc & 1)
                      crc = (crc >> 1) ^ 0xEDB88320;
                  else
                      crc >>= 1;
              }
          }
          addr   += chunk;
          remain -= chunk;
      }
      crc ^= 0xFFFFFFFF;
      // CRC 不匹配 更改标志，回退到现有 APP
      if (crc != file_crc) {
          uint32_t err_msg[3] = {file_len, file_crc, 0x43524345};  // 0x43524345 = "ECRC"
          Flash_Erase_msg();
          IF_Write(FLASH_MSG_START, (uint8_t *)err_msg, sizeof(err_msg));
          NVIC_SystemReset();
      }


      //  擦除 APP 区，搬运固件到内置 Flash
      Flash_Erase_APP();//擦

      uint32_t copy_errors = 0;
      uint8_t  copy_buf[256];
      uint8_t  verify_buf[256];

      for (uint32_t offset = 0; offset < file_len; offset += 256)
      {
          uint16_t chunk = (file_len - offset) > 256 ? 256 : (uint16_t)(file_len - offset);

          // 从外置 Flash 读取
          sFLASH_ReadBuffer(copy_buf, FLASH_EXT_START + offset, chunk);

          // 4 字节对齐补齐
          if (chunk % 4 != 0) {
              uint16_t padded = (chunk + 3) & ~3U;
              for (uint16_t p = chunk; p < padded; p++) copy_buf[p] = 0xFF;
              chunk = padded;
          }
          IF_Write(FLASH_APP_START + offset, copy_buf, chunk);

          //  读回校验
          IF_Read(FLASH_APP_START + offset, verify_buf, chunk);
          if (memcmp(copy_buf, verify_buf, chunk) != 0) {
              copy_errors++;
          }
      }

      //  搬运完毕，判定结果
      if (copy_errors == 0) {
          // 全部校验通过：清除标记，重启运行新固件
          Flash_Erase_msg();
      } else {
          // 校验失败 清除标记
          // 写入错误码 0x434F5059
          uint32_t err_msg[3] = {file_len, file_crc, 0x434F5059};
          Flash_Erase_msg();
          IF_Write(FLASH_MSG_START, (uint8_t *)err_msg, sizeof(err_msg));
      }
      NVIC_SystemReset();
  }
  else
  {
    uint32_t app_sp, app_pc;

    // jump_to_app: 验证 APP 固件有效性
    // [0] = 初始 SP, [1] = Reset_Handler PC
    app_sp = *(__IO uint32_t*)(FLASH_APP_START);
    app_pc = *(__IO uint32_t*)(FLASH_APP_START + 4);

    // SP 必须在 RAM 范围内 (20KB RAM @ 0x20000000)
    // PC 必须在 APP 区范围内，且 LSB=1 (Thumb 模式)
    if ((app_sp >= 0x20000000 && app_sp <= 0x20005000) &&
        (app_pc >= FLASH_APP_START && app_pc <= FLASH_APP_END) &&
        (app_pc & 1))
    {
        init_turn();  // 跳转前关闭所有外设
        hal_app_jump(FLASH_APP_START);
    }

  }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void init_turn(){
  // 跳转前应执行:
  HAL_SPI_DeInit(&hspi1);
  HAL_UART_DeInit(&huart3);
  HAL_DeInit();
  SysTick->CTRL = 0;   // 关闭 SysTick
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
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
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
