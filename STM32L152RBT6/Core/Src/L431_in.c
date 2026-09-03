#include "L431_in.h"
#include "4g_echo.h"
// 缓冲区大小定义
#define RECV_ADXL_BUF_SIZE 1024


extern UART_HandleTypeDef huart3;
static uint8_t recv_ADXL_buf[RECV_ADXL_BUF_SIZE];
static uint16_t ADXL_buf_len = 0;
// 全局变量定义
bool idle3_flag = false;
ADXL_data_t ADXL_process_data = {0};


// 开启USART3接收中断
void nvic3_init() {
    // 开启
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart3, UART_IT_IDLE);
    // 清除中断标志
    __HAL_UART_CLEAR_FLAG(&huart3, UART_FLAG_RXNE);
    __HAL_UART_CLEAR_FLAG(&huart3, UART_FLAG_IDLE);
}

// 空闲中断标记检查
bool do3_IDLE(void) {
    bool ret = idle3_flag;
        idle3_flag = false;
    return ret;
}

// 中断数据接收
void recv3_nvic_data(uint8_t data) {
    if (ADXL_buf_len < RECV_ADXL_BUF_SIZE) {
        recv_ADXL_buf[ADXL_buf_len] = data;
        ADXL_buf_len++;
    } else {
        ADXL_buf_len = 0;
    }
}

// 清空接收缓冲区
void clear_recv_buf(void) {
    memset(recv_ADXL_buf, 0, RECV_ADXL_BUF_SIZE);
    ADXL_buf_len = 0;
}

// 数据处理
void process_data(void) {
    // 空闲中断触发，整包数据接收完成
    if (do3_IDLE() == true) {
        // 检查长度
        if (ADXL_buf_len < sizeof(test_data_t)) {
            clear_recv_buf();
            return;
        }

        // 转换为帧头结构体
        test_data_t *p_test = (test_data_t *)recv_ADXL_buf;

        // 校验帧头
        if (p_test->head != 0xAA) {
            clear_recv_buf();
            return;
        }
        // 校验指令
        if (p_test->cmd != 0x01) {
            clear_recv_buf();
            return;
        }
        // 校验数据长度
        if (ADXL_buf_len < p_test->len || p_test->len > sizeof(ADXL_data_t)) {
            clear_recv_buf();
            return;
        }

        // 转换为加速度数据结构体
        ADXL_data_t *p_adxl = (ADXL_data_t *)recv_ADXL_buf;
        // 提取数据到全局结构体
        ADXL_process_data.acc_x = p_adxl->acc_x;
        ADXL_process_data.acc_y = p_adxl->acc_y;
        ADXL_process_data.acc_z = p_adxl->acc_z;
        ADXL_process_data.pitch = p_adxl->pitch;
        ADXL_process_data.roll = p_adxl->roll;

        // 调试
        //printf("acc_x=%.2f, acc_y=%.2f, acc_z=%.2f, pitch=%.2f, roll=%.2f\n",ADXL_process_data.acc_x, ADXL_process_data.acc_y, ADXL_process_data.acc_z,ADXL_process_data.pitch, ADXL_process_data.roll);

        // 发送
        mqtt_publish(recv_ADXL_buf, ADXL_buf_len);

        clear_recv_buf();
        return;
    }
}



