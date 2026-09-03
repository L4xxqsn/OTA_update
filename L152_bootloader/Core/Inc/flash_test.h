#ifndef __FLASH_TEST_H
#define __FLASH_TEST_H

#include "main.h"
#include "spi.h"

#define W25Q16_CS_LOW()  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_RESET)
#define W25Q16_CS_HIGH() HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET)


uint8_t SPI_ReadWriteByte(uint8_t TxData);
uint8_t W25Q_ReadStatusReg(void);
void W25Q_WaitBusy(void);
void W25Q_WriteEnable(void);
void W25Q_Erase_Sector(uint32_t addr);
void W25Q_PageProgram(uint8_t *buf, uint32_t addr, uint32_t len);
void W25Q_Write(uint8_t *buf, uint32_t addr, uint32_t len);
void W25Q_Read(uint8_t *buf, uint32_t addr, uint32_t len);

#endif /* __FLASH_TEST_H */
