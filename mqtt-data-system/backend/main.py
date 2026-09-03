from fastapi import FastAPI, WebSocket, WebSocketDisconnect, UploadFile, File
from fastapi.middleware.cors import CORSMiddleware
import paho.mqtt.client as mqtt
import json
from datetime import datetime
import struct
import asyncio
import logging
import threading
import os
from typing import Optional

# ========== 日志配置 ==========
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s - %(name)s - %(levelname)s - %(message)s",
    datefmt="%Y-%m-%d %H:%M:%S",
    handlers=[logging.StreamHandler()]
)
logging.getLogger("uvicorn").setLevel(logging.WARNING)
logging.getLogger("uvicorn.access").setLevel(logging.WARNING)
logger = logging.getLogger("adxl-ota-service")

app = FastAPI(title="ADXL355服务 + OTA升级服务")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# MQTT配置
MQTT_CONFIG = {
    "broker": "replace-server-host",
    "port": 1883,
    "pub_topic": "test120",   # 上位机发布主题（发送命令/数据到下位机）
    "sub_topic": "test130",   # 上位机订阅主题（接收下位机回复和数据）
    "client_id": "fastapi_mqtt_client",
    "username": "replace-username",
    "password": "replace-username",
    "keepalive": 60,
    "reconnect_min": 1,
    "reconnect_max": 10
}

# ====================== 协议常量 ======================
HEAD = 0xAB                    # 通用帧头
CMD_FIRST = 0x11               # 首包命令
CMD_DATA = 0x12                # 数据包命令
CMD_ACK_DATA = 0x12            # 下位机数据包 ACK（更新中）
CMD_ACK_COMPLETE = 0x13        # 下位机完成 ACK（更新完成）
DATA_CHUNK_SIZE = 256           # 每包最大数据长度（字节），与 Flash 页大小对齐

# ====================== CRC 算法 ======================
def crc32_ieee(data: bytes) -> int:
    """CRC-32 (IEEE 802.3) 用于包自身与文件整体 CRC 校验"""
    crc = 0xFFFFFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xEDB88320
            else:
                crc >>= 1
    return crc ^ 0xFFFFFFFF

# ====================== 数据包打包/解包 ======================
def pack_first_pkt(file_len: int, file_crc: int, crc: int) -> bytes:
    """head + cmd + crc + file_len + file_crc  小端字节序（匹配 STM32 memcpy）"""
    return struct.pack('<BB I H I', HEAD, CMD_FIRST, crc, file_len, file_crc)

def pack_update_pkt(offset: int, data_len: int, data: bytes, crc: int) -> bytes:
    """head + cmd + crc + offset + data_len + data  小端字节序"""
    return struct.pack(f'<BB I H H {len(data)}s', HEAD, CMD_DATA, crc, offset, data_len, data)

def unpack_pc_update(raw: bytes):
    """
    解包下位机回复 (PC_update_t):
    head(1) + cmd(1) + offset(2)  小端字节序（STM32 Cortex-M3 原生字节序）
    返回 (head, cmd, offset)
    """
    head, cmd, offset = struct.unpack('<BB H', raw)
    return head, cmd, offset

# ====================== 全局 OTA 状态 ======================
ota_state = {
    "bin_data": None,
    "file_size": 0,
    "file_crc": 0,             # 整体文件 CRC32
    "chunk_size": DATA_CHUNK_SIZE,
    "hex_list": [],
    "is_ready": False,

    # 一问一答核心状态
    "awaiting_ack": False,     # 是否等待下位机 ACK
    "acked_offset": -1,        # 下位机已确认的偏移量（字节），仅由 ACK 更新，上位机不修改
    "auto_mode": False,        # 是否开启自动升级模式
    "send_delay": 2.0,         # 数据包发送间隔（秒）
                               # 必须 > MCU mqtt_publish_with_timeout() 执行时间（~1-2s），
                               # 否则新数据包到达时 MCU 仍在等待 AT 命令的 'OK' 响应，
                               # 其 do2_IDLE() 会消耗空闲标志，uart_clear_recv_buf() 会清除固件数据
    "ack_timeout": 8.0,        # 等待 ACK 超时（秒），超时后重发当前数据包
                               # 取值说明：MCU 超时 5s 首轮重发 ACK，Server 超时应略长于 MCU
                               # 以避免同时重传；但要短于 MCU 3次重试耗尽发 cmd=0x11 的时间（~17s）
    "ack_timeout_task": None,  # ACK 超时 asyncio Task 句柄

    "last_sent_packet": b"",   # 缓存最近一次发送的包（用于过滤自环消息）
    "filter_self": True,       # 是否过滤自环消息
    "total_packets": 0
}

# 线程锁，保护 ota_state 的读写
ota_lock = threading.Lock()

# WebSocket 管理器
class ConnectionManager:
    def __init__(self):
        self.active_connections = []
        self.loop = None
        self.lock = threading.Lock()

    async def connect(self, ws):
        await ws.accept()
        if not self.loop:
            self.loop = asyncio.get_running_loop()
        with self.lock:
            self.active_connections.append(ws)

    def disconnect(self, ws):
        with self.lock:
            if ws in self.active_connections:
                self.active_connections.remove(ws)

    async def broadcast(self, msg):
        with self.lock:
            conns = self.active_connections.copy()
        for c in conns:
            try:
                await c.send_text(msg)
            except Exception as e:
                logger.error(f"广播失败: {e}")
                self.disconnect(c)

    def send(self, msg):
        if self.loop:
            asyncio.run_coroutine_threadsafe(self.broadcast(msg), self.loop)

manager = ConnectionManager()

# ====================== ADXL355 数据解析（完全保留） ======================
def parse_adxl355_data(raw_data: bytes) -> Optional[dict]:
    if len(raw_data) < 26:
        return None
    try:
        head, total_len, cmd = struct.unpack("<HHH", raw_data[:6])
        x, y, z, pitch, roll = struct.unpack("<fffff", raw_data[6:26])
        if head != 0x00AA or cmd != 0x01:
            return None
        return {
            "head": f"{head:04x}",
            "cmd": f"{cmd:04x}",
            "result": {
                "x_g": round(x, 6),
                "y_g": round(y, 6),
                "z_g": round(z, 6),
                "pitch": round(pitch, 2),
                "roll": round(roll, 2)
            }
        }
    except Exception as e:
        logger.error(f"ADXL解析失败: {e}")
        return None

# ====================== MQTT 消息处理 ======================
def on_mqtt_connect(client, userdata, flags, rc):
    if rc == 0:
        client.subscribe(MQTT_CONFIG["sub_topic"])
        logger.info("MQTT已连接，发布主题: %s，订阅主题: %s", MQTT_CONFIG["pub_topic"], MQTT_CONFIG["sub_topic"])
    else:
        logger.error(f"MQTT连接失败，错误码: {rc}")

def on_mqtt_message(client, userdata, msg):
    raw = msg.payload

    # 1. 过滤自环消息（自己刚发出去的包直接忽略）
    # 使用锁保护，防止并发读写 last_sent_packet
    with ota_lock:
        if ota_state["filter_self"] and raw == ota_state["last_sent_packet"]:
            logger.debug("过滤MQTT自环消息，不处理")
            return

    # 2. 尝试解析 ADXL 数据（优先，不影响后续 OTA 处理）
    parsed = parse_adxl355_data(raw)
    manager.send(json.dumps({
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "topic": msg.topic,
        "raw_data_hex": raw.hex().upper(),
        "parsed_data": parsed or "error"
    }))

    # 3. 如果是 OTA 自动模式，尝试解析下位机回复 (PC_update_t)
    if ota_state["auto_mode"]:
        asyncio.run_coroutine_threadsafe(process_ack(raw), manager.loop)

def init_mqtt():
    client = mqtt.Client(MQTT_CONFIG["client_id"])
    client.username_pw_set(MQTT_CONFIG["username"], MQTT_CONFIG["password"])
    client.on_connect = on_mqtt_connect
    client.on_message = on_mqtt_message
    client.reconnect_delay_set(min_delay=MQTT_CONFIG["reconnect_min"], max_delay=MQTT_CONFIG["reconnect_max"])
    try:
        client.connect(MQTT_CONFIG["broker"], MQTT_CONFIG["port"], MQTT_CONFIG["keepalive"])
        client.loop_start()
    except Exception as e:
        logger.error(f"MQTT初始化失败: {e}")
    return client

mqtt_client = init_mqtt()

# ====================== ACK 超时管理 ======================
async def _ack_timeout_handler():
    """ACK 超时处理：如果超时未收到下位机 ACK，重发当前数据包"""
    await asyncio.sleep(ota_state["ack_timeout"])
    with ota_lock:
        if not ota_state["auto_mode"] or not ota_state["awaiting_ack"]:
            return
        ota_state["awaiting_ack"] = False
        offset = ota_state["acked_offset"]
    logger.warning("ACK 超时 (offset=%d)，重发当前数据包", offset)
    await send_current_data_packet()

def _cancel_ack_timeout():
    """取消当前 ACK 超时任务"""
    task = ota_state.get("ack_timeout_task")
    if task and not task.done():
        task.cancel()
    ota_state["ack_timeout_task"] = None

def _start_ack_timeout():
    """启动 ACK 超时任务（在事件循环中调度）"""
    _cancel_ack_timeout()
    try:
        loop = asyncio.get_running_loop()
        ota_state["ack_timeout_task"] = loop.create_task(_ack_timeout_handler())
    except RuntimeError:
        pass  # 事件循环未运行（例如在启动期间）

# ====================== 处理下位机 ACK ======================
async def process_ack(raw: bytes):
    """处理下位机 ACK 回复，实现严格的停等协议：
    发一包 → 等 ACK → 取消超时 → 发下一包。
    竞争条件说明：
    MCU 的 mqtt_publish_with_timeout() 在发送完 ACK 负载后仍需等待 AT 命令的 'OK'
    响应（~1-2s）。在此期间若上位机发送新数据包，4G 模块的 +QMTRECV: URC 会到达
    UART，被 mqtt_publish_with_timeout() 内 do2_IDLE() 消耗空闲标志，随后被
    uart_clear_recv_buf() 清除。send_delay 必须 > MCU 完成 AT 命令序列的时间。
    """
    if not ota_state["auto_mode"]:
        return

    # 长度检查：PC_update_t = head(1) + cmd(1) + offset(2) = 4 字节
    if len(raw) != 4 or raw[0] != HEAD:
        logger.debug("收到非 OTA 回复包，忽略")
        return

    head, cmd, offset = unpack_pc_update(raw)
    logger.info(f"收到下位机回复: cmd=0x{cmd:02X}, offset={offset}")

    # ========================================
    # cmd=0x11: 下位机请求重发首包 / 重新开始
    # 不检查 awaiting_ack，因为下位机可能在任意时刻请求重传
    # ========================================
    if cmd == CMD_FIRST:
        logger.warning("下位机请求重发首包 (cmd=0x11), offset=%d", offset)

        _cancel_ack_timeout()

        with ota_lock:
            ota_state["acked_offset"] = -1
            ota_state["awaiting_ack"] = True

        # 重新构建并发送首包
        file_len = ota_state["file_size"]
        file_crc = ota_state["file_crc"]
        crc_buf = struct.pack('<H I', file_len, file_crc)
        crc_val = crc32_ieee(crc_buf)
        first_pkt = pack_first_pkt(file_len, file_crc, crc_val)

        with ota_lock:
            ota_state["last_sent_packet"] = first_pkt
            ota_state["awaiting_ack"] = True
        mqtt_client.publish(MQTT_CONFIG["pub_topic"], first_pkt)
        ota_state["hex_list"].append(f"【重传首包 | cmd=0x11】{first_pkt.hex().upper()}")

        _start_ack_timeout()

        logger.info(f"重新发送首包: len={file_len} file_crc=0x{file_crc:08X} 自身CRC32=0x{crc_val:08X}")
        manager.send(json.dumps({
            "status": "resend_first",
            "msg": "下位机请求重传，已从首包重新开始发送"
        }))
        return

    # ========================================
    # cmd=0x12 / cmd=0x13 需要等待 ACK 状态
    # 加锁检查，避免与 send_current_data_packet 竞争
    # ========================================
    with ota_lock:
        if not ota_state["awaiting_ack"]:
            logger.debug("收到 ACK 但不在等待状态，忽略")
            return

    # ========================================
    # cmd=0x12: 数据包 ACK，正在更新中
    # ========================================
    if cmd == CMD_ACK_DATA:
        pass  # 继续后续单调递增校验

    # ========================================
    # cmd=0x13: 最后一包 ACK，更新完成
    # ========================================
    elif cmd == CMD_ACK_COMPLETE:
        logger.info("下位机上报更新完成 (cmd=0x13)")
        # 仍然走单调递增校验，offset >= file_size 时触发完成逻辑

    else:
        logger.error(f"下位机命令未知: cmd=0x{cmd:02X}")
        _cancel_ack_timeout()
        with ota_lock:
            ota_state["auto_mode"] = False
            ota_state["awaiting_ack"] = False
        manager.send(json.dumps({"status": "error", "msg": f"下位机命令未知: cmd=0x{cmd:02X}"}))
        return

    # ========== 单调递增校验（cmd=0x12 / cmd=0x13） ==========
    with ota_lock:
        prev_offset = ota_state["acked_offset"]
        file_size = ota_state["file_size"]

    if offset < prev_offset:
        logger.warning("ACK offset 过期 (prev=%d, cur=%d)，丢弃", prev_offset, offset)
        return

    if offset == prev_offset:
        # 下位机重复确认同一 offset → 说明上一包数据丢失，重发当前数据包
        # 竞争窗口：下位机此时可能仍在 mqtt_publish_with_timeout() 中，
        # send_delay 必须足够大以确保下位机已退出阻塞发送函数
        logger.warning("重复 ACK (offset=%d)，下位机未收到数据包，重发", offset)
        _cancel_ack_timeout()
        with ota_lock:
            ota_state["awaiting_ack"] = False
        await asyncio.sleep(ota_state["send_delay"])
        await send_current_data_packet()
        return

    # ========== 更新已确认偏移量，取消超时 ==========
    _cancel_ack_timeout()
    with ota_lock:
        ota_state["acked_offset"] = offset
        ota_state["awaiting_ack"] = False

    logger.info("ACK 校验通过: offset %d -> %d (下位机已确认 %d/%d 字节)",
                prev_offset, offset, offset, file_size)

    # ========== 发送完成判断 ==========
    if offset >= file_size:
        logger.info("所有数据发送完成并已确认（下位机 offset=%d, 文件大小=%d）", offset, file_size)
        with ota_lock:
            ota_state["auto_mode"] = False
        manager.send(json.dumps({
            "status": "complete",
            "offset": offset,
            "file_size": file_size,
            "total_packets": ota_state["total_packets"],
            "msg": "全部发送完成，等待下位机重启"
        }))
        return

    # ========== 发送下一个数据块（延迟确保下位机退出阻塞发送） ==========
    await asyncio.sleep(ota_state["send_delay"])
    await send_current_data_packet()

# ====================== 发送当前数据包 ======================
async def send_current_data_packet():
    """发送下一个数据块，起始位置由下位机已确认的 acked_offset 决定"""
    with ota_lock:
        offset = ota_state["acked_offset"]   # 下位机已确认的偏移量 = 下一包的起始位置
        chunk_size = ota_state["chunk_size"]
        data = ota_state["bin_data"]
        file_size = ota_state["file_size"]

    if data is None:
        return

    # 如果已经在等待 ACK，不重复发送（防止正反馈循环）
    with ota_lock:
        if ota_state["awaiting_ack"]:
            logger.debug("send_current_data_packet: 仍在等待 ACK，跳过发送 offset=%d", offset)
            return

    remaining = file_size - offset
    if remaining <= 0:
        return
    send_len = min(chunk_size, remaining)
    chunk = data[offset: offset + send_len]

    # 计算 CRC32（仅对 data 部分）
    crc_val = crc32_ieee(chunk)

    # 打包：head, cmd, crc, offset, data_len, data
    final_pkt = pack_update_pkt(offset, send_len, chunk, crc_val)

    # 缓存并发送（注意：acked_offset 不在此处修改，只等下位机 ACK 更新）
    with ota_lock:
        ota_state["last_sent_packet"] = final_pkt
        ota_state["awaiting_ack"] = True
    mqtt_client.publish(MQTT_CONFIG["pub_topic"], final_pkt)
    ota_state["hex_list"].append(f"【offset {offset}】{final_pkt.hex().upper()}")

    progress = (offset / file_size) * 100 if file_size > 0 else 0
    manager.send(json.dumps({
        "status": "sending",
        "offset": offset,
        "file_size": file_size,
        "total_packets": ota_state["total_packets"],
        "progress": round(progress, 1)
    }))
    logger.info(f"发送数据块 offset={offset}, len={send_len}, CRC32=0x{crc_val:08X}，等待下位机ACK...")

    # 启动 ACK 超时计时器，防止因 MQTT 丢包导致永久等待
    _start_ack_timeout()

# ====================== 前端接口：上传 BIN 文件 ======================
@app.post("/ota/upload_bin")
async def upload_bin(file: UploadFile = File(...)):
    """接收前端上传的 BIN 文件，计算文件 CRC16 和分包数"""
    try:
        if not file.filename.endswith(".bin"):
            return {"status": "error", "msg": "仅支持.bin文件"}

        bin_data = await file.read()
        file_size = len(bin_data)
        if file_size == 0:
            return {"status": "error", "msg": "空文件"}
        if file_size > 65535:
            return {"status": "error", "msg": f"固件过大 ({file_size} 字节)，协议限制最大 65535 字节 (uint16_t)"}

        # 计算整体文件 CRC32
        file_crc = crc32_ieee(bin_data)
        
        # 计算总包数
        with ota_lock:
            total_packets = (file_size + ota_state["chunk_size"] - 1) // ota_state["chunk_size"]

            ota_state["bin_data"] = bin_data
            ota_state["file_size"] = file_size
            ota_state["file_crc"] = file_crc
            ota_state["total_packets"] = total_packets
            ota_state["is_ready"] = True
            ota_state["hex_list"] = []
            ota_state["acked_offset"] = -1

        logger.info(f"接收BIN: {file.filename} 大小={file_size} CRC32=0x{file_crc:08X} 分包数={total_packets}")
        return {
            "status": "ok",
            "file_size": file_size,
            "file_crc": file_crc,
            "total_packets": total_packets,
            "chunk_size": ota_state["chunk_size"]
        }
    except Exception as e:
        logger.error(f"上传BIN失败: {e}")
        return {"status": "error", "msg": str(e)}

# ====================== 开始自动升级 ======================
@app.get("/ota/start_auto")
async def start_auto():
    if not ota_state["is_ready"] or ota_state["bin_data"] is None:
        return {"status": "error", "msg": "请先上传BIN"}

    with ota_lock:
        ota_state["auto_mode"] = True
        ota_state["awaiting_ack"] = True
        ota_state["acked_offset"] = -1   # 等待下位机首包 ACK (offset=0)

    # 构建首包（first_pkt_t）
    file_len = ota_state["file_size"]
    file_crc = ota_state["file_crc"]

    # CRC32 只计算 file_len + file_crc (4 字节)
    crc_buf = struct.pack('<H I', file_len, file_crc)
    crc_val = crc32_ieee(crc_buf)
    first_pkt = pack_first_pkt(file_len, file_crc, crc_val)

    # 缓存并发送
    with ota_lock:
        ota_state["last_sent_packet"] = first_pkt
    mqtt_client.publish(MQTT_CONFIG["pub_topic"], first_pkt)
    ota_state["hex_list"].append(f"【首包】{first_pkt.hex().upper()}")

    _start_ack_timeout()

    logger.info(f"发送首包: len={file_len} file_crc=0x{file_crc:08X} 自身CRC32=0x{crc_val:08X}")
    return {"status": "ok", "msg": "已发送首包，等待下位机应答"}

# ====================== 前端接口：清空 / 重置 OTA 状态 ======================
@app.get("/ota/clear")
async def clear_ota():
    """停止当前 OTA 发送循环，重置到初始状态（从首包重新开始）"""
    _cancel_ack_timeout()

    # 关闭自动模式
    with ota_lock:
        ota_state["auto_mode"] = False
        ota_state["awaiting_ack"] = False

    # 重置核心状态
    with ota_lock:
        ota_state["acked_offset"] = -1
        ota_state["last_sent_packet"] = b""
        ota_state["hex_list"] = []
        # 保留 bin_data / file_size / file_crc / is_ready / total_packets
        # 用户可直接再次点击"开始升级"，无需重新上传

    logger.info("OTA 传输已停止并重置，可重新开始发送首包")
    return {"status": "ok", "msg": "OTA 已重置，可从首包重新开始"}

# ====================== 前端接口：获取 16 进制日志 ======================
@app.get("/ota/hex")
async def get_hex_list():
    return {"hex_list": ota_state["hex_list"]}

# ====================== 原有 WebSocket 接口 ======================
@app.websocket("/ws/ota")
async def ws_ota(ws: WebSocket):
    await manager.connect(ws)
    try:
        while True:
            await asyncio.sleep(1)
    except WebSocketDisconnect:
        manager.disconnect(ws)
        logger.info("OTA WebSocket连接断开")

@app.websocket("/ws/data")
async def ws_data(ws: WebSocket):
    await manager.connect(ws)
    try:
        while True:
            await asyncio.sleep(1)
    except WebSocketDisconnect:
        manager.disconnect(ws)
        logger.info("数据WebSocket连接断开")

# ====================== 健康检查接口 ======================
@app.get("/health")
async def health():
    return {
        "status": "ok",
        "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
        "mqtt_connected": mqtt_client.is_connected(),
        "ota_ready": ota_state["is_ready"],
        "ota_auto_mode": ota_state["auto_mode"],
        "file_size": ota_state["file_size"],
        "acked_offset": ota_state["acked_offset"]
    }

if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000, reload=False)