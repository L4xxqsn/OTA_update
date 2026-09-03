#include "flash_test.h"


/* 底层 SPI 读写字节 */
uint8_t SPI_ReadWriteByte(uint8_t TxData)
{
    uint8_t RxData;
    HAL_SPI_TransmitReceive(&hspi1, &TxData, &RxData, 1, HAL_MAX_DELAY);
    return RxData;
}

/* 读状态寄存器 */
uint8_t W25Q_ReadStatusReg(void)
{
    uint8_t status;
    W25Q16_CS_LOW();
    SPI_ReadWriteByte(0x05);
    status = SPI_ReadWriteByte(0xFF);
    W25Q16_CS_HIGH();
    return status;
}

/* 等待空闲 */
void W25Q_WaitBusy(void)
{
    while (W25Q_ReadStatusReg() & 0x01);
}

/* 写使能 */
void W25Q_WriteEnable(void)
{
    W25Q16_CS_LOW();
    SPI_ReadWriteByte(0x06);
    W25Q16_CS_HIGH();
}

/* 扇区擦除 */
void W25Q_Erase_Sector(uint32_t addr)
{
    W25Q_WriteEnable();
    W25Q16_CS_LOW();
    SPI_ReadWriteByte(0x20);
    SPI_ReadWriteByte((addr >> 16) & 0xFF);
    SPI_ReadWriteByte((addr >> 8) & 0xFF);
    SPI_ReadWriteByte(addr & 0xFF);
    W25Q16_CS_HIGH();
    W25Q_WaitBusy();
}

/* 单页编程 */
void W25Q_PageProgram(uint8_t *buf, uint32_t addr, uint32_t len)
{
    W25Q_WriteEnable();
    W25Q16_CS_LOW();
    SPI_ReadWriteByte(0x02);
    SPI_ReadWriteByte((addr >> 16) & 0xFF);
    SPI_ReadWriteByte((addr >> 8) & 0xFF);
    SPI_ReadWriteByte(addr & 0xFF);
    for (uint32_t i = 0; i < len; i++) {
        SPI_ReadWriteByte(buf[i]);
    }
    W25Q16_CS_HIGH();
    W25Q_WaitBusy();
}

/* 多页写入 */
void W25Q_Write(uint8_t *buf, uint32_t addr, uint32_t len)
{
    while (len > 0) {
        uint32_t page_remain = 256 - (addr & 0xFF);
        uint32_t write_len = len > page_remain ? page_remain : len;
        W25Q_PageProgram(buf, addr, write_len);
        addr += write_len;
        buf += write_len;
        len -= write_len;
    }
}

/* 多页读出 */
void W25Q_Read(uint8_t *buf, uint32_t addr, uint32_t len)
{
    W25Q16_CS_LOW();
    SPI_ReadWriteByte(0x03);
    SPI_ReadWriteByte((addr >> 16) & 0xFF);
    SPI_ReadWriteByte((addr >> 8) & 0xFF);
    SPI_ReadWriteByte(addr & 0xFF);
    for (uint32_t i = 0; i < len; i++) {
        buf[i] = SPI_ReadWriteByte(0xFF);
    }
    W25Q16_CS_HIGH();
}