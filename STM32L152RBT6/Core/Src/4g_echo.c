#include "4g_echo.h"
#include "flash_io.h"

extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

bool idle2_flag = false;

uint8_t recv_buf[RECV_BUF_SIZE] = {0};
uint16_t recv_buf_len = 0;
uint8_t mqtt_send_buf[RECV_BUF_SIZE] = {0};
static uint32_t s_crc32;

//OTA升级全局变量

PC_update_t pc_pkt;                         // 回复包实体
uint16_t    file_len;                       // 固件总长度
uint32_t    file_crc;                       // 固件 CRC

//接收超时重发配置
#define DATA_TIMEOUT_MS   5000              // 接收超时
#define MAX_RESEND_ACK    3                 // 最大重发 ACK 次数


//超时重发状态变量
static uint32_t g_last_recv_tick = 0;       // 最后一次收到有效数据包的时间
static uint8_t  g_resend_ack_cnt = 0;       // 当前重发 ACK 计数
static uint16_t offset_now;                 // 当前offset的存储

//MQTT入网状态机配置表
const MQTT_State_T mqtt_state_table[] = {
    [STATE_SEND_AT] = {
        .send_data = "AT\r\n",
        .expect_recv = "OK",
        .delay_ms = 1000
    },
    [STATE_SEND_ATQ] = {
        .send_data = "ATE0\r\n",
        .expect_recv = "OK",
        .delay_ms = 1000
    },
    [STATE_SEND_CSQ] = {
        .send_data = "AT+CSQ\r\n",
        .expect_recv = "+CSQ:",
        .delay_ms = 5000
    },
    [STATE_SEND_CGDCONT] = {
        .send_data = "AT+CGDCONT=1,\"IP\",\"CTNET\"\r\n",
        .expect_recv = "OK",
        .delay_ms = 2000
    },
    [STATE_SEND_CGACT] = {
        .send_data = "AT+CGACT=1,1\r\n",
        .expect_recv = "OK",
        .delay_ms = 8000
    },
    [STATE_SEND_QMTCFG_RECV] = {
        .send_data = "AT+QMTCFG=\"recv/mode\",0,0,1\r\n",
        .expect_recv = "OK",
        .delay_ms = 10000
    },
    [STATE_SEND_QMTOPEN] = {
        .send_data = "AT+QMTOPEN=0,\"replace-server-host\",1883\r\n",
        .expect_recv = "+QMTOPEN: 0,0",
        .delay_ms = 15000
    },
    [STATE_SEND_QMTCONN] = {
        .send_data = "AT+QMTCONN=0,\"mqtt_101010a\",\"replace-username\",\"replace-username\"\r\n",
        .expect_recv = "+QMTCONN: 0,0,0",
        .delay_ms = 10000
    },
    [STATE_SEND_QMTSUB] = {
        .send_data = "AT+QMTSUB=0,1,\"test120\",0\r\n",
        .expect_recv = "+QMTSUB: 0,1,0,0",
        .delay_ms = 5000
    },
    [STATE_MQTT_READY] = {
        .send_data = "",
        .expect_recv = "",
        .delay_ms = 0
    }
};




//内置Flash写入函数
void IF_Write(uint32_t write_addr, uint8_t *data, uint16_t len)
{
  HAL_FLASH_Unlock();
  uint32_t *p32 = (uint32_t *)data;
  for (uint16_t i = 0; i < len / 4; i++)
  {
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, write_addr + i * 4, p32[i]);
  }
  HAL_FLASH_Lock();
}


//USART2中断初始化
void nvic2_init() {
    if (!__HAL_UART_GET_IT_SOURCE(&huart2, UART_IT_RXNE)) {
        __HAL_UART_ENABLE_IT(&huart2, UART_IT_RXNE);
    }
    if (!__HAL_UART_GET_IT_SOURCE(&huart2, UART_IT_IDLE)) {
        __HAL_UART_ENABLE_IT(&huart2, UART_IT_IDLE);
    }
    // 清除中断标志
    __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_RXNE);
    __HAL_UART_CLEAR_FLAG(&huart2, UART_FLAG_IDLE);
    idle2_flag = false;
}


//读取空闲中断标志
bool do2_IDLE(void) {
    bool ret = idle2_flag;
    idle2_flag = false;
    return ret;
}


//   中断数据接收
void recv2_nvic_data(uint8_t data) {
    if (recv_buf_len < RECV_BUF_SIZE) {
        recv_buf[recv_buf_len++] = data;
    }
}


//   电源引脚初始化
void Start_Init(void) {
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_13, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_14, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);

    HAL_Delay(30);
    HAL_GPIO_WritePin(GPIOH, GPIO_PIN_1, GPIO_PIN_SET);
    HAL_Delay(15000);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
}


//   清空接收缓冲区
void uart_clear_recv_buf(uint8_t *recv_buf, uint16_t *recv_buf_len, size_t buf_size) {
    memset(recv_buf, 0, buf_size);
    *recv_buf_len = 0;
    //idle2_flag = false;
}


//   串口发送函数
void uart_send_data(const char* data) {
    if (data == NULL || strlen(data) == 0) {
        return;
    }
    HAL_UART_Transmit(&huart2, (uint8_t*)data, strlen(data), 100);
}

//   MQTT联网初始化
bool mqtt_net_init(MQTT_State current_state) {
    uint32_t start_tick = 0;
    uint8_t try_times = 3;  // 每个状态3次出错

    // 循环执行直到全部完成或重试耗尽
    while (current_state < STATE_MQTT_READY && try_times > 0) {

        uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
        const MQTT_State_T* state_cfg = &mqtt_state_table[current_state];
        // 发送AT指令
        uart_send_data(state_cfg->send_data);
        start_tick = HAL_GetTick();
        bool is_match = false;

        // 等待响应
        while (1) {
            uint32_t elapsed_ms = HAL_GetTick() - start_tick;
            if (elapsed_ms >= state_cfg->delay_ms) {
                break;
            }
            if (strstr((const char*)recv_buf, state_cfg->expect_recv) != NULL) {
                is_match = true;
                break;
            }
        }
        if (is_match) {
            // 状态成功，切换到下一个状态，重置重试次数
            current_state++;
            try_times = 3;
        } else {
            // 状态失败重试，重试耗尽则跳下一状态
            try_times--;

            uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
            if(try_times <=0){
                current_state++;
            }
        }
    }
    uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
    idle2_flag = false;
    return (current_state == STATE_MQTT_READY) ? true : false;
}

//flash读取crc失败重发函数
static void ota_reset_and_restart(void)
{
    file_len = 0;
    file_crc = 0;

    pc_pkt.head   = 0xAB;
    pc_pkt.cmd    = 0x11;   // 请求重发首包
    pc_pkt.offset = 0x0000;

    g_last_recv_tick = 0;
    g_resend_ack_cnt = 0;

    mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000);
}

//包crc失败重发函数
static void ota_pkt_restart(void)
{

      pc_pkt.head = 0xAB;
      pc_pkt.cmd = 0x12;
      // offset 保持不变，重发一次 ACK
      // 注意：不刷新 g_last_recv_tick，让超时机制能正常工作

      mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000);
}

//处理MQTT订阅消息
void mqtt_process_subscribe_msg(void)
{
    uint8_t payload[512];
    uint16_t pay_len = 0;
    //有数据来，重发次数重置

    if (!do2_IDLE())
    {
        return;
    }
    g_resend_ack_cnt = 0;
    // 注意：不在这里刷新 g_last_recv_tick，由 mqtt_get_packet() 中仅新包匹配时才刷新，
    // 避免重传包反复重置计时器导致超时永不触发
    //解析MQTT订阅消息,开始解at封装包
    if (mqtt_parse_urc(recv_buf, recv_buf_len, payload, &pay_len))
    {
        //将at包中数据读到payload数组后，清空recv
        uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
        //正确收到OTA数据，
        mqtt_get_packet(payload, &pay_len);
    }
        //接收网络中断信息:+QMTSTAT:"0,1"
      else if (recv_buf != NULL && strstr((char*)recv_buf, "+QMTSTAT:\"0,1\"") != NULL) {
          mqtt_net_init(STATE_SEND_QMTOPEN);
      }



    uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
}


//   消息转发到UART3
void turn_message_to3(void){
    HAL_UART_Transmit(&huart3,(const uint8_t*)recv_buf,recv_buf_len,100);
    uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
}

//   MQTT发布函数
bool mqtt_publish_with_timeout(uint8_t *send_data, uint16_t data_len, uint32_t timeout_ms)
{
    char mqtt_publish_buf[256] = {0};
    uint32_t start;
    bool got_prompt = false;

    // 记录当前缓冲区末尾，后续只搜索此位置之后到达的新数据，
    // 避免匹配上一次 publish 残留的 '>' / 'OK' / 'ERROR'，
    // 同时不清空缓冲区，保护已到达但尚未解析的 OTA 数据
    uint16_t search_start = recv_buf_len;
    idle2_flag = false;

    // 发送 MQTT 发布命令
    sprintf(mqtt_publish_buf, "AT+QMTPUBEX=0,0,0,0,\"%s\",%d\r\n", "test130", data_len);
    uart_send_data(mqtt_publish_buf);

    // 等待 '>' 提示符
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (do2_IDLE()) {
            if (recv_buf_len > search_start &&
                memchr(recv_buf + search_start, '>', recv_buf_len - search_start)) {
                got_prompt = true;
                break;
            }
            if (recv_buf_len > search_start &&
                strstr((char *)(recv_buf + search_start), "ERROR")) {
                return false;
            }
        }
        HAL_Delay(1);
    }
    if (!got_prompt) {
        return false;
    }

    // 发送数据负载
    HAL_UART_Transmit(&huart2, send_data, data_len, 100);

    // 更新搜索起点：数据发送后到达的响应从此处开始
    search_start = recv_buf_len;

    // 等待最终结果 OK/ERROR
    start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (do2_IDLE()) {
            if (recv_buf_len > search_start &&
                strstr((char *)(recv_buf + search_start), "OK")) {
                return true;
            }
            if (recv_buf_len > search_start &&
                strstr((char *)(recv_buf + search_start), "ERROR")) {
                return false;
            }
        }
        HAL_Delay(1);
    }
    return false;
}

//数据包处理
void mqtt_get_packet(uint8_t *payload, uint16_t *payload_len)
{
    // 最小长度检查
    if (*payload_len < 2) return;
    if (payload[0] != 0xAB) return;

    uint8_t cmd = payload[1];

    //cmd == 0x11 首包
    if (cmd == 0x11)
    {
        if (*payload_len < 12) return;

        uint32_t pkt_crc;
        uint16_t len;
        uint32_t fcrc;
        memcpy(&pkt_crc, payload + 2, 4);  // crc
        memcpy(&len, payload + 6, 2);      // file_len
        memcpy(&fcrc, payload + 8, 4);     // file_crc

        // 计算CRC校验
        uint32_t calc_crc = CRC32_Calc(payload + 6, 6);

        //在不使用局部变量后，进行清除
        uart_clear_recv_buf(payload, payload_len, 512);

        if (calc_crc != pkt_crc) return;

        file_len = len;
        file_crc = fcrc;
        //擦除外置flash存储
        sFlash_Erease_All();

        pc_pkt.head = 0xAB;
        pc_pkt.cmd = 0x12;        // cmd=0x12 表示正在更新中
        pc_pkt.offset = 0x0000;

        g_last_recv_tick = HAL_GetTick();  // 首包接收成功，启动超时计时
        g_resend_ack_cnt = 0;

        mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000);

        return;
    }

    //cmd == 0x12 数据包
    if (cmd == 0x12)
    {
        if (*payload_len < 10) {
            return;
        }

        uint32_t data_crc;
        uint16_t data_offset;
        uint16_t data_len;

        memcpy(&data_crc, payload + 2, 4);     // crc
        memcpy(&data_offset, payload + 6, 2);   // offset
        memcpy(&data_len, payload + 8, 2);      // data_len

        // CRC 校验
        uint32_t calc_crc = CRC32_Calc(payload + 10, data_len);
        if (calc_crc != data_crc) {
            return;
        }

        // 下位机 offset 与数据包 offset 匹配
        if (pc_pkt.offset == data_offset)
        {
            // 仅新包（offset 匹配）才刷新超时计时器；
            // 重传包走 else-if 分支调用 ota_pkt_restart()，不刷新
            g_last_recv_tick = HAL_GetTick();

            // 判断是否为最后一包数据
            if (pc_pkt.offset + data_len >= file_len && file_len != 0) {

                // 写入最后一包数据
                sFLASH_WritePage(payload + 10, FLASH_EXT_START + (uint32_t)pc_pkt.offset, data_len);
                  //在不使用局部变量后，进行清除
                uart_clear_recv_buf(payload, payload_len, 512);

                //读出来进行crc校验
                uint8_t crc_Buffer[256]= {0};
                sFLASH_ReadBuffer(crc_Buffer, FLASH_EXT_START + (uint32_t)pc_pkt.offset, data_len);
                uint32_t read_crc = CRC32_Calc(crc_Buffer, data_len);
                if (read_crc != data_crc) {
                    // 写后读 CRC 错，请求重传整个固件
                    ota_reset_and_restart();
                    return;
                }
                //循环读出来进行crc校验
                uint32_t start_address = FLASH_EXT_START;
                uint8_t result = FileCRC(start_address, file_len, file_crc);

                if (result) {
                    // 最后一包：ACK 必须携带 file_size 以通知 Server 完成；
                    // offset 先推进再发送，与 Server 的 offset >= file_size 判断一致
                    pc_pkt.offset += data_len;

                    pc_pkt.head = 0xAB;
                    pc_pkt.cmd = 0x13;        // cmd=0x13 表示最后一包/更新完成

                    mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000);

                    uint32_t flag[3] = {file_len, file_crc, 0x55AA55AA};
                    IF_Write(FLASH_NOTE_START, (uint8_t *)flag, sizeof(flag));
                    HAL_Delay(500);
                    NVIC_SystemReset();
                    //return;  // 防止编译器警告/意外贯穿
                } else {
                    // 全文件 CRC 校验失败，请求上位机重新开始
                    uart_clear_recv_buf(payload, payload_len, 512);
                    ota_reset_and_restart();
                    return;
                }
            }

            // 非最后一包：写入 Flash 并 ACK
            sFLASH_WritePage(payload + 10, FLASH_EXT_START + (uint32_t)pc_pkt.offset, data_len);

            //读出来进行crc校验
            uint8_t crc_Buffer[256]= {0};
            sFLASH_ReadBuffer(crc_Buffer, FLASH_EXT_START + (uint32_t)pc_pkt.offset, data_len);
            uint32_t read_crc = CRC32_Calc(crc_Buffer, data_len);
            if (read_crc != data_crc) {
                // 写后读 CRC 错，请求重传整个固件
                ota_reset_and_restart();
                return;
            }

            //在不使用局部变量后，进行清除
            uart_clear_recv_buf(payload, payload_len, 512);

            // offset 先推进再发 ACK：ACK 语义为"已收到并写入 offset 之前的所有数据"
            // 竞态窗口由 Server 端 send_delay(2.0s) 关闭——Server 等 MCU 退出
            // mqtt_publish_with_timeout() 后才发下一包，新数据不会在 ACK 期间到达
            pc_pkt.head = 0xAB;
            pc_pkt.cmd = 0x12;        // cmd=0x12 表示正在更新中
            pc_pkt.offset += data_len;

            if (!mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000)) {
                // ACK 发送失败（AT命令超时/ERROR），MQTT发布状态不确定
                // 不回溯 offset——让 MCU 超时重发 ACK 或 Server 超时重发数据包来恢复
            }
            return;
        }
        // 重复包：下位机 offset > 数据包 offset，已写入过，重发当前 ACK
        else if (pc_pkt.offset > data_offset)
        {
            //在不使用局部变量后，进行清除
            uart_clear_recv_buf(payload, payload_len, 512);

            ota_pkt_restart();

            return;
        }
        // 异常：下位机 offset < 数据包 offset，请求上位机重新开始
        else if (pc_pkt.offset < data_offset)
        {
            //在不使用局部变量后，进行清除
            uart_clear_recv_buf(payload, payload_len, 512);
            ota_reset_and_restart();
            return;
        }
    }
}



//MQTT URC消息解析
bool mqtt_parse_urc(uint8_t *buf, uint16_t len, uint8_t *payload, uint16_t *payload_len)
{
    // 找到帧头 +QMTRECV:
    char *head = strstr((char*)buf, "+QMTRECV:");
    if (!head) return false;

    // 跳过帧头前缀 "+QMTRECV:" 共9个字符
    char *p = head + 9;
    int remain = len - (p - (char*)buf);
    if (remain <= 0) return false;
    // 跳过空格
    if (*p == ' ') { p++; remain--; }

    // 跳过 client_idx
    while (remain>0 && (*p >= '0' && *p <= '9')) { p++; remain--; }
    if (remain>0 && *p == ',') { p++; remain--; }

    // 跳过 msgid
    while (remain>0 && (*p >= '0' && *p <= '9')) { p++; remain--; }
    if (remain>0 && *p == ',') { p++; remain--; }

    // 跳过 "topic" 字段，直到找到第二个双引号
    while (remain>0 && *p != '"')  { p++; remain--; }  // 跳过第一个 "
    p++; remain--;
    while (remain>0 && *p != '"')  { p++; remain--; }  // 跳过 topic 内容
    p++; remain--;

    // 跳过逗号
    if (remain>0 && *p == ',') { p++; remain--; }

    // 读取 payload 长度
    uint16_t pay_len = 0;
    while (remain>0 && *p >= '0' && *p <= '9') {
        pay_len = pay_len * 10 + (*p - '0');
        p++; remain--;
    }

    // 跳过逗号和引号，找到数据起始位置
    while (remain>0 && *p != ',') { p++; remain--; }
    if (remain>0 && *p == ',') { p++; remain--; }
    while (remain>0 && *p != '"') { p++; remain--; }
    if (remain>0 && *p == '"') { p++; remain--; }

    // 复制二进制数据
    if (pay_len == 0 || remain < pay_len)
        return false;

    memcpy(payload, (uint8_t*)p, pay_len);
    *payload_len = pay_len;

    return true;
}

//下位机接收超时重发 ACK
void mqtt_ota_timeout_check(void)
{
    // 未在更新中则跳过
    if (file_len == 0) {
        return;
    }

    // 未收到过任何包则跳过
    if (g_last_recv_tick == 0) {
        return;
    }

    uint32_t now = HAL_GetTick();
    uint32_t elapsed = now - g_last_recv_tick;

    // 未超时，什么都不做
    if (elapsed < DATA_TIMEOUT_MS) {
        return;
    }

    // 先检查重发次数是否耗尽
    if (g_resend_ack_cnt >= MAX_RESEND_ACK) {
        // 重发次数耗尽，回到等待状态，请求上位机重新发送首包
        g_last_recv_tick = 0;
        g_resend_ack_cnt = 0;

        pc_pkt.head = 0xAB;
        pc_pkt.cmd  = 0x11;
        pc_pkt.offset = 0;

        mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000);
        return;
    }

    // 重发次数未耗尽：重发当前 ACK，退避等待

    if(offset_now == pc_pkt.offset){
    g_resend_ack_cnt++;
    }
    offset_now = pc_pkt.offset;
    // 重发 第1次等2s, 第2次等4s, 第3次等6s, 之后放弃请求重传首包
    g_last_recv_tick = now + (g_resend_ack_cnt * 2000 - DATA_TIMEOUT_MS);
    // 等效于 下一次超时触发在 cnt*2 秒后

    pc_pkt.head = 0xAB;
    pc_pkt.cmd  = 0x12;
    // pc_pkt.offset 保持不变

    mqtt_publish_with_timeout((uint8_t *)&pc_pkt, sizeof(PC_update_t), 5000);
}


//====== crc函数 ======
void CRC32_Init(void)
{
    s_crc32 = 0xFFFFFFFF;
}

void CRC32_Update(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        s_crc32 ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (s_crc32 & 1)
                s_crc32 = (s_crc32 >> 1) ^ 0xEDB88320;
            else
                s_crc32 >>= 1;
        }
    }
}

uint32_t CRC32_Final(void)
{
    return s_crc32 ^ 0xFFFFFFFF;
}

//====== 连续crc校验文件 ======
bool FileCRC(uint32_t start_addr, uint32_t file_len, uint32_t file_crc)
{
    uint32_t remaining = file_len;
    uint32_t offset = 0;
    uint8_t buffer[MQTT_SEND_BUF_SIZE];
    CRC32_Init();

    //将数据依次读取出来进行crc
    while (remaining > 0)
    {
        uint16_t chunk = (remaining > MQTT_SEND_BUF_SIZE) ? MQTT_SEND_BUF_SIZE : (uint16_t)remaining;

        sFLASH_ReadBuffer(buffer, start_addr + offset, chunk);
        CRC32_Update(buffer, chunk);

        offset += chunk;
        remaining -= chunk;
    }

    // 全部数据读取完成，计算最终 CRC 并与期望值比较
    uint32_t calc_crc = CRC32_Final();
    if (calc_crc != file_crc)
    {
        return false;   // CRC 不匹配
    }

    return true;       // 成功
}


void mqtt_publish(uint8_t *send_data, uint16_t data_len) {

    uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);  // 清空接收缓冲区

    // 组装AT指令
    sprintf((char*)mqtt_send_buf, "AT+QMTPUBEX=0,0,0,0,\"%s\",%d\r\n","test129" ,data_len);

    // 发送指令
    uart_send_data((char*)mqtt_send_buf);
    memset(mqtt_send_buf, 0, MQTT_SEND_BUF_SIZE);

    uint32_t start_tick = 0;
    start_tick = HAL_GetTick();

    // 等待 '>' 提示
    while (1) {
        uint32_t elapsed_ms = HAL_GetTick() - start_tick;
        if (elapsed_ms >= 15000) {
            break;
        }
        // 收到回复
        if (do2_IDLE())
        {
            if (strstr((char*)recv_buf, ">"))
            {
                HAL_UART_Transmit(&huart2, send_data, data_len, 100);
            }
            if (strstr((char*)recv_buf, "ERROR"))
            {
                break;
            }
        }
    }
    uart_clear_recv_buf(recv_buf, &recv_buf_len, RECV_BUF_SIZE);
}
