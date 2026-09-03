#include "bootloader_app.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>


typedef void (*pFunction)(void);
#define APP_START 0x0800A000    // app_start_address
#define APP_END   0x0801FFFF    // app_end_address

// Internal flash: message area address range
#define FLASH_MSG_START 0x08008100
#define FLASH_MSG_END   0x08009000

#define PAGE_SIZE 256



// erase_internal_flash_app_area
void Flash_Erase_APP(void)
{
  FLASH_EraseInitTypeDef ei={0};
  uint32_t pageErr=0;

  HAL_FLASH_Unlock();

  ei.TypeErase=FLASH_TYPEERASE_PAGES;
  ei.PageAddress=APP_START;
  ei.NbPages=((APP_END - APP_START) + PAGE_SIZE - 1) / PAGE_SIZE;

  if (HAL_FLASHEx_Erase(&ei, &pageErr) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_FLASH_Lock();

}

// erase_internal_flash_msg_area
void Flash_Erase_msg(void)
{
  FLASH_EraseInitTypeDef ei={0};
  uint32_t pageErr=0;

  HAL_FLASH_Unlock();

  ei.TypeErase=FLASH_TYPEERASE_PAGES;
  ei.PageAddress=FLASH_MSG_START;
  ei.NbPages=((FLASH_MSG_END - FLASH_MSG_START) + PAGE_SIZE - 1) / PAGE_SIZE;

  if (HAL_FLASHEx_Erase(&ei, &pageErr) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_FLASH_Lock();

}

// write_internal_flash (4-byte aligned)
void IF_Write(uint32_t write_addr, uint8_t *data, uint16_t len)
{
  // require_4byte_alignment
  if ((write_addr % 4) != 0 || (len % 4) != 0) return;

  HAL_FLASH_Unlock();

  for (uint16_t i = 0; i < len; i += 4)
  {
    // 使用 memcpy 避免非对齐访问导致的 HardFault（Cortex-M3 不支持非对齐访问）
    uint32_t word;
    memcpy(&word, data + i, sizeof(word));

    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_addr + i, word);
  }

  HAL_FLASH_Lock();
}

// read_internal_flash
void IF_Read(uint32_t read_addr, uint8_t *data, uint16_t len)
{
  for(uint16_t i=0; i<len; i++)
  {
    data[i] = *(uint8_t*)(read_addr +i);
  }
}
