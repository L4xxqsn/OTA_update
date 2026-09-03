#ifndef __BOOTLOADER_APP_H
#define __BOOTLOADER_APP_H
#include "main.h"
#include "spi.h"


// erase_internal_flash_app_area
void Flash_Erase_APP(void);

// write_internal_flash
void IF_Write(uint32_t write_addr, uint8_t *data, uint16_t len);

// read_internal_flash
void IF_Read(uint32_t read_addr, uint8_t *data, uint16_t len);

// erase_internal_flash_msg_area
void Flash_Erase_msg(void);
#endif
