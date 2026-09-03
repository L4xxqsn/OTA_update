
#include "usart.h"


//串口3发送函数
HAL_StatusTypeDef uart3_send_data(uint8_t *pData, uint16_t len)
{
    // 参数合法性检查
    if (pData == NULL || len == 0 || len > UART3_RX_BUF_SIZE)
    {
        return HAL_ERROR;
    }

    // 轮询方式发送数据
    return HAL_UART_Transmit(&huart3, pData, len, UART3_RX_TIMEOUT);
}

//串口3接收初始化
void uart3_receive_init(void)
{
    // 重置接收状态
    uart3_rx_len = 0;
    uart3_rx_complete_flag = 0;
    // 开启串口2的中断接收，接收1个字节就触发中断
    HAL_UART_Receive_IT(&huart3, &uart3_rx_buf[uart3_rx_len], 1);
}

//获取串口3接收的数据
uint16_t uart3_get_receive_data(uint8_t *buf, uint16_t max_len)
{
    if (buf == NULL || max_len == 0 || uart3_rx_complete_flag == 0)
    {
        return 0;
    }

    // 防止目标缓冲区溢出
    uint16_t copy_len = (uart3_rx_len < max_len) ? uart3_rx_len : max_len;
    // 复制接收的数据到外部缓冲区
    for (uint16_t i = 0; i < copy_len; i++)
    {
        buf[i] = uart3_rx_buf[i];
    }
    
    return copy_len;
}

//清除串口3接收完成标志
void uart3_clear_receive_flag(void)
{
    uart3_rx_len = 0;
    uart3_rx_complete_flag = 0;
    // 重新开启中断，准备下一次接收
    HAL_UART_Receive_IT(&huart3, &uart3_rx_buf[uart3_rx_len], 1);
}

// 检查串口3是否接收完成
uint8_t uart3_is_receive_complete(void)
{
    return uart3_rx_complete_flag;
}


//串口错误回调函数
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART3)
    {
        // 发生错误时重新初始化接收
        uart3_receive_init();
    }
}
