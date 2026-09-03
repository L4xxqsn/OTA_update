/***
 ************************************************************************************
 * @file     spi_flash.c
 * @version  V1.0.1
 * @brief    W25Q16 SPI Flash 底层驱动——读写擦除及应用层存储接口
 ************************************************************************************
 * @description
 *
 *  基于 STM32L152RBT6 HAL 库，操作 W25Q16 (2MB) SPI NOR Flash。
 *  移植自官方标准固件库示例，应用层可存储心跳时间、MAC、4G保活时间等。
 *
 *  硬件连接:
 *    SPI1:  PA5-SCK, PA6-MISO, PA7-MOSI
 *    CS:    PB11 (FLASH_NSS)
 *    HOLD:  PB10
 *
 *  数据存储格式 (每条记录):
 *    [0x49] [0x50] [原始数据...] [CRC32 4字节, 小端序]
 *    总占用: 2(帧头) + sizeof(数据结构) + 4(CRC32) = 实际数据长度 + 6
 ************************************************************************************
***/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "flash_io.h"
#include "main.h"


/* ============================================================
 *  应用层相关宏定义（#ifndef 允许用户在 user.h 头文件预定义覆盖）
 *  以下为默认值，用户应根据实际 Flash 布局修改地址值
 * ============================================================ */
#ifndef FLASH_SECTOR_SIZE
#define FLASH_SECTOR_SIZE           ((uint32_t)4096)   /* W25Q16 扇区大小: 4KB  */
#endif

#ifndef FLASH_HEART_TIME_ADDR
#define FLASH_HEART_TIME_ADDR       ((uint32_t)0x000000) /* TODO: 按实际分区填写心跳时间存储地址 */
#endif

#ifndef FLASH_MAC_ADDR
#define FLASH_MAC_ADDR              ((uint32_t)0x001000) /* TODO: 按实际分区填写 MAC 地址存储地址 */
#endif

#ifndef FLASH_4G_KEEPTIME_ADDR
#define FLASH_4G_KEEPTIME_ADDR      ((uint32_t)0x010000) /* TODO: 按实际分区填写4G保持时间存储地址 */
#endif

#ifndef ERROR_FLASHPARAM
#define ERROR_FLASHPARAM            ((uint32_t)0x00000004) /* Flash 参数 CRC 校验错误 */
#endif

/* ============================================================
 *  外部全局变量声明（定义在 user.c / main.c 中）
 * ============================================================ */
extern uint32_t G_Err_No;                     /* 全局错误码寄存器          */
extern struct HEART_TIME G_Heart_Time;        /* 心跳时间数据结构体         */
extern uint8_t  G_MAC_Addr[];                 /* MAC 地址数组              */
extern uint32_t G_4G_KeepConnect_Time;        /* 4G 模块保持连接时间        */

/* ============================================================
 *  SPI 句柄配置
 * ============================================================ */
extern SPI_HandleTypeDef hspi1;
#define SFLASH_Handler hspi1

/* 底层函数前置声明 */
void sFLASH_LowLevel_DeInit(void);
void sFLASH_LowLevel_Init(void);

extern void HAL_SPI_MspDeInit(SPI_HandleTypeDef* hspi);
extern void HAL_SPI_MspInit(SPI_HandleTypeDef* hspi);

/* ============================================================
 *  MISO 引脚模式切换（解决 SPI 从设备占用 MISO 时使用）
 * ============================================================ */

/**
  * @brief  释放 MISO 引脚为模拟模式
  * @note   将 PA6 设为模拟输入模式，防止 SPI 从设备占用 MISO 时总线冲突。
  *         同时降低功耗。下次 SPI 通信前需调用 spi_MISO_Init() 恢复。
  * @param  None
  * @retval None
  */
void spi_MISO_DeInit()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin   = GPIO_PIN_6;
    GPIO_InitStruct.Mode  = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    GPIO_InitStruct.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/**
  * @brief  恢复 MISO 引脚为 SPI 复用功能
  * @note   将 PA6 设为 AF5 (SPI1_MISO) 推挽复用模式，准备 Flash 通信。
  *         由 sFLASH_CS_LOW() 在每次 SPI 传输开始前调用。
  * @param  None
  * @retval None
  */
void spi_MISO_Init()
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin       = GPIO_PIN_6;
    GPIO_InitStruct.Mode      = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull      = GPIO_NOPULL;
    GPIO_InitStruct.Speed     = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

/* ============================================================
 *  片选 (CS/NSS) 控制
 * ============================================================ */

/**
  * @brief  拉低 Flash 片选 (CS/NSS)，使能芯片通信
  *         - 先初始化 MISO 引脚复用功能
  *         - HOLD 引脚拉高，保证 Flash 正常工作
  *         - CS 拉低，选中 Flash 芯片
  * @param  None
  * @retval None
  */
void sFLASH_CS_LOW(){
    spi_MISO_Init();
    HAL_GPIO_WritePin(FLASH_HOLD_GPIO_Port, FLASH_HOLD_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(FLASH_NSS_GPIO_Port, FLASH_NSS_Pin, GPIO_PIN_RESET);
}

/**
  * @brief  拉高 Flash 片选 (CS/NSS)，释放芯片
  *         - CS 拉高，释放 Flash 芯片
  *         - HOLD 拉低释放
  *         - 释放 MISO 引脚为模拟模式，降低功耗、避免总线冲突
  * @param  None
  * @retval None
  */
void sFLASH_CS_HIGH(){
    HAL_GPIO_WritePin(FLASH_NSS_GPIO_Port, FLASH_NSS_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(FLASH_HOLD_GPIO_Port, FLASH_HOLD_Pin, GPIO_PIN_RESET);
    spi_MISO_DeInit();
}

/* ============================================================
 *  初始化
 * ============================================================ */


/**
  * @brief  初始化 SPI Flash 引脚（CS 拉高反选 Flash）
  * @param  None
  * @retval None
  */
void sFLASH_Init(void)
{
  /* 反选 Flash: 片选拉高 */
  sFLASH_CS_HIGH();
}

/* ============================================================
 *  状态寄存器读取
 * ============================================================ */

/**
  * @brief  读取 Flash 状态寄存器1 (S0~S7)
  *         - bit0: WIP  (写/擦除忙标志, 1=忙)
  *         - bit1: WEL  (写使能锁存, 1=已使能)
  *         - bit2~3: BP0~BP1 (块保护位)
  *         - bit5~6: 保留
  *         - bit7: SRP (状态寄存器保护)
  * @param  None
  * @retval 状态寄存器1当前值
  */
uint8_t sFLASH_ReadStatusReg1()
{
    sFLASH_CS_LOW();
    uint8_t status = 0;
    sFLASH_SendByte(sFLASH_CMD_RDSR);      /* 发 RDSR 命令 0x05 */
    status = sFLASH_SendByte(sFLASH_DUMMY_BYTE);
    sFLASH_CS_HIGH();
    return status;
}

/**
  * @brief  读取 Flash 状态寄存器2
  *         - bit3: CMP (互补保护位)
  *         - bit6: QE  (Quad Enable, 四线SPI使能)
  *         - bit7: SUS (挂起/暂停功能状态)
  * @param  None
  * @retval 状态寄存器2当前值
  */
uint8_t sFLASH_ReadStatusReg2()
{
    sFLASH_CS_LOW();
    uint8_t status = 0;
    sFLASH_SendByte(0x35);                 /* 发 RDSR2 命令 */
    status = sFLASH_SendByte(sFLASH_DUMMY_BYTE);
    sFLASH_CS_HIGH();
    return status;
}

/* ============================================================
 *  忙等待
 * ============================================================ */

/**
  * @brief  查询等待 Flash 忙状态（简化版）
  *         - 发送读状态寄存器1命令 (RDSR, 0x05)
  *         - 循环读取并检测 WIP 位 (bit0)，直到为0
  * @note   未使用 static 变量，每次调用都重新初始化循环，
  *         确保每次都实际读取硬件状态寄存器。
  *         与 sFLASH_WaitForWriteEnd() 功能等价，代码更简洁。
  * @param  None
  * @retval None
  */
void sFLASH_WaitBusy()
{
    uint8_t status = 1;
    while(status)
    {
        sFLASH_CS_LOW();
        sFLASH_SendByte(sFLASH_CMD_RDSR);
        status = sFLASH_SendByte(sFLASH_DUMMY_BYTE);
        status = status & 0x01;
        sFLASH_CS_HIGH();
    }
}

/* ============================================================
 *  擦除操作
 * ============================================================ */

/**
  * @brief  擦除指定扇区 (4KB)
  * @note   擦除前自动发送写使能 (WREN)，擦除后查询等待完成。
  *         擦除后扇区所有位恢复为 0xFF。
  *         W25Q16 典型扇区擦除时间: ~45ms (max 400ms)
  * @param  SectorAddr: 要擦除的扇区起始地址（24位，芯片内部寻址）
  * @retval None
  */
void sFLASH_EraseSector(uint32_t SectorAddr)
{
  /* 发送写使能指令 */
  sFLASH_WriteEnable();

  /* 扇区擦除时序: CS低 → SE(0x20) → 24位地址 → CS高 → 等待 */
  sFLASH_CS_LOW();
  sFLASH_SendByte(sFLASH_CMD_SE);
  sFLASH_SendByte((SectorAddr & 0xFF0000) >> 16);
  sFLASH_SendByte((SectorAddr & 0xFF00) >> 8);
  sFLASH_SendByte(SectorAddr & 0xFF);
  sFLASH_CS_HIGH();

  /* 等待擦除完成 */
  sFLASH_WaitForWriteEnd();
}

/**
  * @brief  全片擦除（Block Erase 64KB 方式）
  * @note   *** 注意 *** sFLASH_CMD_BE 定义为 0xD8，实际是 Block Erase (64KB)。
  *          并非真正的 Chip Erase (0xC7/0x60)。
  *          如需真正的全片擦除，请将命令改为 0xC7。
  *          当前函数名为 Bulk 及历史遗留代码，实际为单次 64KB 块擦除。
  * @param  None
  * @retval None
  */
void sFLASH_EraseBulk(void)
{
  /* 发送写使能指令 */
  sFLASH_WriteEnable();

  /* 块擦除时序: CS低 → BE(0xD8) → CS高 → 等待 */
  sFLASH_CS_LOW();
  sFLASH_SendByte(sFLASH_CMD_BE);
  sFLASH_CS_HIGH();

  /* 等待擦除完成 */
  sFLASH_WaitForWriteEnd();
}

/**
  * @brief  应用层全范围擦除函数
  * @note   从 FLASH_HEART_TIME_ADDR 开始，以 FLASH_SECTOR_SIZE 为步进，
  *         循环擦除到 FLASH_4G_KEEPTIME_ADDR。
  *         用于用户数据存储区域的批量初始化。
  * @param  None
  * @retval None
  */
void sFlash_Erease_All()
{
    for(uint32_t begin = FLASH_HEART_TIME_ADDR;
        begin <= FLASH_4G_KEEPTIME_ADDR;
        begin += FLASH_SECTOR_SIZE)
    {
        sFLASH_EraseSector(begin);
    }
}

/* ============================================================
 *  写操作
 * ============================================================ */

/**
  * @brief  写使能——发送 WREN 命令 (0x06)
  * @note   写/擦除操作前必须调用，Flash 内部会将 WEL 位设为1。
  *         写完一页或一次擦除后 WEL 自动清零，下次写/擦前需重新使能。
  * @param  None
  * @retval None
  */
void sFLASH_WriteEnable(void)
{
  sFLASH_CS_LOW();
  sFLASH_SendByte(sFLASH_CMD_WREN);
  sFLASH_CS_HIGH();
}

/**
  * @brief  查询等待写/擦除操作完成
  * @note   循环发送 RDSR 命令并检测 WIP 位 (bit0)。
  *         为1则继续等待，为0则操作完成。
  * @param  None
  * @retval None
  */
void sFLASH_WaitForWriteEnd(void)
{
  uint8_t flashstatus = 0;

  sFLASH_CS_LOW();
  sFLASH_SendByte(sFLASH_CMD_RDSR);

  /* 循环等待直到 WIP 位清零 */
  do
  {
    /* 发送空字节产生时钟，同时读取状态寄存器值 */
    flashstatus = sFLASH_SendByte(sFLASH_DUMMY_BYTE);
  }
  while ((flashstatus & sFLASH_WIP_FLAG) == SET);

  sFLASH_CS_HIGH();
}

/**
  * @brief  页写入——写入不超过一页 (256字节) 的数据
  * @note   写入前自动发送写使能 (WREN)。
  *         *** 调用者必须保证 WriteAddr + NumByteToWrite 不跨页 ***
  *         跨页写入请使用 sFLASH_WriteBuffer()。
  * @param  pBuffer: 源数据缓冲区指针
  * @param  WriteAddr: Flash 内部目标地址 (24位)
  * @param  NumByteToWrite: 写入字节数 (≤ sFLASH_SPI_PAGESIZE = 256)
  * @retval None
  */
void sFLASH_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
  /* 写使能 */
  sFLASH_WriteEnable();

  /* 页编程时序: CS低 → PP(0x02) → 24位地址 → 数据... → CS高 */
  sFLASH_CS_LOW();
  sFLASH_SendByte(sFLASH_CMD_WRITE);
  sFLASH_SendByte((WriteAddr & 0xFF0000) >> 16);
  sFLASH_SendByte((WriteAddr & 0xFF00) >> 8);
  sFLASH_SendByte(WriteAddr & 0xFF);

  /* 循环发送写入数据 */
  while (NumByteToWrite--)
  {
    sFLASH_SendByte(*pBuffer);
    pBuffer++;
  }

  sFLASH_CS_HIGH();

  /* 等待写入完成 */
  sFLASH_WaitForWriteEnd();
}

/**
  * @brief  缓冲写入——支持跨页写入任意长度数据
  * @note   自动处理写入地址的页对齐和跨页拆分：
  *         1. 若起始地址非页对齐，先写当前页剩余部分
  *         2. 中间写完整页
  *         3. 最后写剩余不足一页的部分
  *         这是应用层最常用的写入接口。
  * @param  pBuffer: 源数据缓冲区指针
  * @param  WriteAddr: Flash 内部目标地址 (24位)
  * @param  NumByteToWrite: 写入总字节数（支持任意长度）
  * @retval None
  */
void sFLASH_WriteBuffer(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
  uint8_t NumOfPage = 0, NumOfSingle = 0, Addr = 0, count = 0, temp = 0;

  /* 计算起始地址在页内的偏移及剩余空间 */
  Addr = WriteAddr % sFLASH_SPI_PAGESIZE;
  count = sFLASH_SPI_PAGESIZE - Addr;       /* 当前页剩余可写字节数 */
  NumOfPage  = NumByteToWrite / sFLASH_SPI_PAGESIZE;
  NumOfSingle = NumByteToWrite % sFLASH_SPI_PAGESIZE;

  if (Addr == 0) /* 起始地址页对齐 */
  {
    if (NumOfPage == 0) /* 数据不足一页 */
    {
      sFLASH_WritePage(pBuffer, WriteAddr, NumByteToWrite);
    }
    else /* 数据超过一页 */
    {
      /* 先写完整页 */
      while (NumOfPage--)
      {
        sFLASH_WritePage(pBuffer, WriteAddr, sFLASH_SPI_PAGESIZE);
        WriteAddr += sFLASH_SPI_PAGESIZE;
        pBuffer   += sFLASH_SPI_PAGESIZE;
      }
      /* 再写剩余 */
      sFLASH_WritePage(pBuffer, WriteAddr, NumOfSingle);
    }
  }
  else /* 起始地址非页对齐 */
  {
    if (NumOfPage == 0) /* 数据不足一页 */
    {
      if (NumOfSingle > count) /* 数据跨越页边界 */
      {
        temp = NumOfSingle - count;
        /* 先写当前页剩余 */
        sFLASH_WritePage(pBuffer, WriteAddr, count);
        WriteAddr += count;
        pBuffer   += count;
        /* 再写下一页起始部分 */
        sFLASH_WritePage(pBuffer, WriteAddr, temp);
      }
      else /* 数据不跨页 */
      {
        sFLASH_WritePage(pBuffer, WriteAddr, NumByteToWrite);
      }
    }
    else /* 数据超过一页 */
    {
      NumByteToWrite -= count;
      NumOfPage  = NumByteToWrite / sFLASH_SPI_PAGESIZE;
      NumOfSingle = NumByteToWrite % sFLASH_SPI_PAGESIZE;

      /* 先写当前页剩余部分 */
      sFLASH_WritePage(pBuffer, WriteAddr, count);
      WriteAddr += count;
      pBuffer   += count;

      /* 写完整页 */
      while (NumOfPage--)
      {
        sFLASH_WritePage(pBuffer, WriteAddr, sFLASH_SPI_PAGESIZE);
        WriteAddr += sFLASH_SPI_PAGESIZE;
        pBuffer   += sFLASH_SPI_PAGESIZE;
      }

      /* 写最后剩余 */
      if (NumOfSingle != 0)
      {
        sFLASH_WritePage(pBuffer, WriteAddr, NumOfSingle);
      }
    }
  }
}

/* ============================================================
 *  读取操作
 * ============================================================ */

/**
  * @brief  缓冲读取——发送 Flash 读指令并读取数据
  * @note   最常用的读接口，支持任意长度和地址。
  * @param  pBuffer: 目标缓冲区指针（接收数据）
  * @param  ReadAddr: Flash 内部源地址 (24位)
  * @param  NumByteToRead: 读取字节数
  * @retval None
  */
void sFLASH_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
  /* 读时序: CS低 → READ(0x03) → 24位地址 → 读取数据... → CS高 */
  sFLASH_CS_LOW();

  sFLASH_SendByte(sFLASH_CMD_READ);
  sFLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
  sFLASH_SendByte((ReadAddr & 0xFF00) >> 8);
  sFLASH_SendByte(ReadAddr & 0xFF);

  /* 连续读取数据，每个时钟周期 Flash 自动递增地址 */
  while (NumByteToRead--)
  {
    *pBuffer = sFLASH_SendByte(sFLASH_DUMMY_BYTE);
    pBuffer++;
  }

  sFLASH_CS_HIGH();
}

/**
  * @brief  启动一个连续读取序列——发送 READ 命令 + 3字节地址后保持 CS 低
  * @note   与 sFLASH_ReadBuffer() 不同，此函数仅发送命令和地址后 CS 保持低电平，
  *         之后可使用 sFLASH_ReadByte() 逐字节循环读取 Flash 数据，
  *         读取完毕后需手动调用 sFLASH_CS_HIGH() 结束传输。
  *         适用于需要自定义读取节奏的场景。
  * @param  ReadAddr: 24位 Flash 内部起始地址
  * @retval None
  */
void sFLASH_StartReadSequence(uint32_t ReadAddr)
{
  sFLASH_CS_LOW();

  /* 发送 READ 命令 (0x03) */
  sFLASH_SendByte(sFLASH_CMD_READ);

  /* 发送 24 位起始地址 */
  sFLASH_SendByte((ReadAddr & 0xFF0000) >> 16);
  sFLASH_SendByte((ReadAddr & 0xFF00) >> 8);
  sFLASH_SendByte(ReadAddr & 0xFF);

  /* CS 保持低——后续通过 sFLASH_ReadByte() 逐字节读取，最后手动 sFLASH_CS_HIGH() */
}

/**
  * @brief  在连续读取序列中读取一个字节
  * @note   需在 sFLASH_StartReadSequence() 之后调用。
  *         每次调用读取一个字节，Flash 内部地址自动+1。
  * @param  None
  * @retval 从 Flash 读出的字节
  */
uint8_t sFLASH_ReadByte(void)
{
  return (sFLASH_SendByte(sFLASH_DUMMY_BYTE));
}

/* ============================================================
 *  SPI 底层收发 & Flash ID 读取
 * ============================================================ */

/**
  * @brief  通过 SPI 收发一个字节
  * @note   使用 HAL_SPI_TransmitReceive 实现全双工收发，
  *         发送参数 byte 的同时接收 Flash 返回的数据。
  *         超时时间 100ms。
  * @param  byte: 要发送的字节
  * @retval 接收到的字节
  */
uint8_t sFLASH_SendByte(uint8_t byte)
{
  uint8_t rcvByte;
  HAL_SPI_TransmitReceive(&SFLASH_Handler, &byte, &rcvByte, 1, 100);
  return rcvByte;
}



/* ============================================================
 *  CRC32 校验
 * ============================================================ */

/**
  * @brief  CRC32 计算（多项式 0xEDB88320，兼容 zlib/gzip 标准）
  * @note   初始值 0xFFFFFFFF，结果异或 0xFFFFFFFF 输出。
  *         逐字节处理，适合嵌入式实时计算。
  * @param  data: 待计算数据指针
  * @param  len:  数据长度（字节）
  * @retval 32位 CRC 校验值
  */
uint32_t CRC32_Calc(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

/* ============================================================
 *  应用层级存储接口
 * ============================================================
 *  数据存储格式: [帧头0x49] [帧头0x50] [原始数据] [CRC32 4字节, 小端序]
 *  总占用: 2(帧头) + sizeof(DataStruct) + 4(CRC32) = 实际数据长度 + 6
 *  读取时: 校验帧头 + CRC32，通过则拷贝到全局变量，失败则使用默认值
 * ============================================================ */

/**
  * @brief  将一段数据保存到指定扇区（封装: 先擦除再写入）
  * @note   先擦除 SectorAddr 所在扇区，再写入数据。
  *         调用者需保证数据长度不超过一个扇区 (4KB) 的容量。
  * @param  SectorAddr: 目标扇区地址
  * @param  pData: 数据缓冲区指针
  * @param  Len: 数据长度 (字节)
  * @retval None
  */
void sFlash_Save_Data(uint32_t SectorAddr, uint8_t * pData, uint16_t Len)
{
    sFLASH_EraseSector(SectorAddr);
    sFLASH_WriteBuffer(pData, SectorAddr, Len);
}






/**
  * @}
  */

/**
  * @}
  */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
