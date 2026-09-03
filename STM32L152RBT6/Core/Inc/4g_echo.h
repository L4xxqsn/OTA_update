#ifndef __4G_ECHO_H
#define __4G_ECHO_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "main.h"


#define MQTT_SEND_BUF_SIZE 256

#define RECV_BUF_SIZE 512

//all built-in Flash
#define FLASH_INT_START ((uint32_t)0x08000000)
#define FLASH_INT_END   ((uint32_t)0x0801FFFF)
//built-in Flash boot loader

#define FLASH_BOOT_START ((uint32_t)0x08000000)
#define FLASH_BOOT_END   ((uint32_t)0x08008000)

//built-in Flash boot
#define FLASH_NOTE_START ((uint32_t)0x08008100)
#define FLASH_NOTE_END   ((uint32_t)0x08009000)

//built-in Flash app
#define FLASH_APP_START ((uint32_t)0x0800A000)
#define FLASH_APP_END   ((uint32_t)0x0801FFFF)


//built-out Flash
#define FLASH_EXT_START ((uint32_t)0x000000)
#define FLASH_EXT_END   ((uint32_t)0x1FFFFF)


// MQTT入网状态机
typedef enum {
    STATE_SEND_AT = 0,
    STATE_SEND_ATQ,
    STATE_SEND_CSQ,
    STATE_SEND_CGDCONT,
    STATE_SEND_CGACT,
    STATE_SEND_QMTCFG_RECV,
    STATE_SEND_QMTOPEN,
    STATE_SEND_QMTCONN,
    STATE_SEND_QMTSUB,
    STATE_MQTT_READY
} MQTT_State;

// mqtt数组格式
typedef struct {
    const char* send_data;
    const char* expect_recv;
    uint32_t delay_ms;
} MQTT_State_T;

#pragma pack(1)


//mcu_recv_head_pkt
typedef struct {
  uint8_t head;
  uint8_t cmd;
  uint32_t crc;
  uint16_t file_len;
  uint32_t file_crc;
}first_pkt_t;

//mcu_recv_data_pkt
typedef struct{
  uint8_t head;
  uint8_t cmd;
  uint32_t crc;
  uint16_t offset;
  uint16_t data_len;
  uint8_t data[];
}update_pkt_t;

//mcu_transmit_pkt
typedef struct{
  uint8_t head;
  uint8_t cmd;
  uint16_t offset;
}PC_update_t;

#pragma pack()




extern UART_HandleTypeDef huart2;
extern bool idle2_flag;
extern uint8_t recv_buf[RECV_BUF_SIZE];
extern uint16_t recv_buf_len;
extern uint8_t mqtt_send_buf[RECV_BUF_SIZE];


uint32_t CRC32_Calc(const uint8_t *buf, uint32_t len);
void IF_Write(uint32_t write_addr, uint8_t *data, uint16_t len);
void recv2_nvic_data(uint8_t data);
void Start_Init(void);
void nvic2_init(void);
bool mqtt_net_init(MQTT_State current_state);
void uart_clear_recv_buf(uint8_t *recv_buf, uint16_t *recv_buf_len, size_t buf_size) ;

bool mqtt_publish_with_timeout(uint8_t *send_data, uint16_t data_len, uint32_t timeout_ms);
void mqtt_publish(uint8_t *send_data, uint16_t data_len);
void mqtt_process_subscribe_msg(void);
bool mqtt_parse_urc(uint8_t *buf, uint16_t len, uint8_t *payload, uint16_t *payload_len);
void mqtt_get_packet(uint8_t *payload, uint16_t *payload_len);
void mqtt_ota_timeout_check(void);
void turn_message_to3(void);

void CRC32_Init(void);
void CRC32_Update(const uint8_t *data, uint32_t len);
uint32_t CRC32_Final(void);
bool FileCRC(uint32_t start_addr, uint32_t file_len, uint32_t file_crc);



#endif