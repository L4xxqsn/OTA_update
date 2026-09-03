#ifndef __SPI_FLASH_H
#define __SPI_FLASH_H

#include "main.h"

#define sFLASH_CMD_WRITE          0x02  /* Write to Memory instruction */
#define sFLASH_CMD_WRSR           0x01  /* Write Status Register instruction */
#define sFLASH_CMD_WREN           0x06  /* Write enable instruction */
#define sFLASH_CMD_READ           0x03  /* Read from Memory instruction */
#define sFLASH_CMD_RDSR           0x05  /* Read Status Register instruction  */
#define sFLASH_CMD_RDID           0x9F  /* Read identification */
#define sFLASH_CMD_SE             0x20  /* Sector Erase instruction */
#define sFLASH_CMD_BE             0xD8  /* Bulk Erase instruction */

#define sFLASH_WIP_FLAG           0x01  /* Write In Progress (WIP) flag */

#define sFLASH_DUMMY_BYTE         0xA5

#define sFLASH_SPI_PAGESIZE       256

#define  sFLASH_ID                  0XEF4015     //W25Q16


/* Exported macro ------------------------------------------------------------*/


/* Exported functions ------------------------------------------------------- */

/* High layer functions  */
void sFLASH_DeInit(void);
void sFLASH_Init(void);
void sFLASH_EraseSector(uint32_t SectorAddr);
void sFLASH_EraseBulk(void);
void sFLASH_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void sFLASH_WriteBuffer(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
void sFLASH_ReadBuffer(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
uint32_t sFLASH_ReadID(void);
void sFLASH_StartReadSequence(uint32_t ReadAddr);

/* Low layer functions */
uint8_t sFLASH_ReadByte(void);
uint8_t sFLASH_SendByte(uint8_t byte);
void sFLASH_WriteEnable(void);
void sFLASH_WaitForWriteEnd(void);

extern void sFlash_Erease_All();
extern void sFLASH_Read_UserParameter();
extern void sFlash_Save_DETECT_PARAM();
extern void sFlash_Save_DETECT_THRESHOLD();
extern void sFlash_Save_HEART_TIME();
extern void sFlash_Save_PM_MOD();
extern void sFlash_Save_MAC();
extern void sFlash_Save_NB_SERVER1();
extern void sFlash_Save_NB_SERVER2();
extern void sFlash_Save_CC2530_SENSITIVITY();
extern void sFlash_Save_FLOATREF_SWITCH();
extern void sFlash_Save_NB_LOWRSSI();
extern void sFlash_Save_NB_LOCKFCN();
extern void sFlash_Save_LOG_SWITCH();
extern void sFlash_Save_LF_REF();
extern void sFlash_Save_LF_THR();
extern void sFlash_Save_4G_KEEPTIME();
extern void sFlash_Save_LF_RATE();
extern void sFlash_Save_LF_OLDVALUE();

#endif /* __SPI_FLASH_H */


