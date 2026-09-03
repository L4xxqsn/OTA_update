#ifndef __L431_IN_H
#define __L431_IN_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include "main.h"





#pragma pack(1)

typedef struct {
    uint8_t head;
    uint16_t len;
    uint16_t cmd;
} test_data_t;

typedef struct {
    uint8_t head;
    uint16_t len;
    uint16_t cmd;
    float acc_x;
    float acc_y;
    float acc_z;
    float pitch;
    float roll;
} ADXL_data_t;

#pragma pack()

// 全局变量
extern bool idle3_flag;
extern ADXL_data_t ADXL_process_data;


void nvic3_init(void);
bool do3_IDLE(void);
void recv3_nvic_data(uint8_t data);
void process_data(void);
void clear_recv_buf(void);  


#endif