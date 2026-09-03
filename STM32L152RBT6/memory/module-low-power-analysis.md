---
name: module-low-power-analysis
description: Comprehensive low-power mode analysis for all 7 modules in the IoT OTA system
metadata:
  type: project
---

# 7 模块低功耗模式 (不耗电模式) 全面分析

## 总体电源拓扑 (Power Tree)

```
Battery (Li-Ion ~3.7V)
  │
  ├─→ [MP4560DN] ──→ 4V Rail ──→ [MD5718] ──→ 4G Module Power
  │      ↑CTL_4V(PB13)        ↑CTL_4VOE(PB14)
  │
  ├─→ 1.8V Regulator ──→ STM32L152 + TXS0108E(VCCA)
  │      ↑CTL_1V8(PB1)
  │
  ├─→ [KT6328A BLE] (always powered, minimal consumption)
  │
  ├─→ [W25Q16] (SPI Flash, always powered, standby mode)
  │
  └─→ VCC_DRV(PC13) ──→ TXS0108E(VCCB)
```

## 模块逐一分析

---

### 1. EC800E (移远 4G LTE 模块)

| 指标 | 数值 | 备注 |
|------|------|------|
| **完全断电 (本项目采用)** | **0 µA** | 通过 7 路 GPIO 完全切断供电 |
| PSM 模式 | ~5 µA | 中国运营商支持不稳定，未采用 |
| CFUN=0 Sleep | ~60 µA | 关闭射频，模块仍有一定的功耗 |
| eDRX Sleep (PF=256) | ~300 µA | 需要运营商 eDRX 支持 |
| Idle (USB 断开) | ~4 mA | 仅注册网络，无数据传输 |
| FDD 发射 @23dBm | ~478-544 mA | 峰值电流可达 2A |

**当前实现**：完全断电策略 → 休眠期间 4G 模块功耗为 0 µA。详见 `pm_state_machine.c` 的 `Module_PowerOff()` 和 `Start_Init_Pins()`。
- 醒来后完全重新上电 + 搜网注册约 10-16 秒
- 控制引脚 PB13(CTL_4V), PB14(CTL_4VOE), PB1(CTL_1V8), PC13(VCC_DRV), PH1(PWRKEY), PB2(DTR), PC2(RESET_N)

**优化空间**：如果运营商实测支持 PSM，可在断电前先发 AT+CPSMS 进入 PSM，下次唤醒后 MCU 先尝试快速恢复连接 (可能节省 8-10 秒搜网时间)。

---

### 2. TXS0108E (TI 8 位双向电平转换器)

| 指标 | 数值 | 备注 |
|------|------|------|
| **OE=LOW (禁用/Hi-Z)** | **~8 µA max** | 内部上拉电阻全部切断 |
| OE=HIGH (使能) | ~20-30 µA typ | One-shot 加速器工作 |
| VCCA 供电 | 1.65-3.6V | VCCA 接 1.8V rail |
| VCCB 供电 | 2.3-5.5V | VCCB 接 VCC_DRV(PC13) |

**当前实现**：
- 休眠时 OE_TXS0108(PA4)=LOW + VCC_DRV(PC13)=LOW → TXS0108E 几乎完全断电
- VCCA 侧由 1.8V rail (CTL_1V8) 控制 → 休眠时 1.8V 也关断
- 实测休眠功耗：接近 0 µA (VCCB 完全切断)
- 恢复时需要重新拉高 OE_TXS0108 和 VCC_DRV

代码位置 `pm_state_machine.c:274-368` `pm_enter_stop()` 中也有处理 GPIO 模拟模式。

**GPIO 对应关系**：
- TXS0108E 用于 4G 模块 (1.8V) 和 STM32 (3.3V/4V IO) 之间的电平转换
- 通道：USART2 TX/RX (PA2/PA3)，以及可能的 STATUS_4G, RI, DTR, PWRKEY 等

---

### 3. STM32L152RBT6 (MCU)

| 模式 | 典型电流 @3.0V/25°C | 最大 @25°C |
|------|---------------------|------------|
| **STOP + RTC LSI (休眠期)** | **~1.50 µA** | 2.11 µA |
| STOP (no RTC) | ~1.10 µA | 1.60 µA |
| Standby + RTC LSI | ~0.89 µA | — |
| **SLEEP WFI (浅睡期)** | ~150-200 µA | — |
| **Run @MSI 2.1MHz, SCALE3** | ~230-300 µA | — |

**当前实现** (详见 `pm_state_machine.c:274-368` `pm_enter_stop()`):
1. ✅ 关外设时钟：USART2, USART3, SPI1, ADC1
2. ✅ GPIO 设模拟模式：PA2/3, PC10/11, PA5/6/7 (消除漏电)
3. ✅ SuspendTick → STOP mode WFI
4. ✅ SCALE3 电压调节器 → 最低功耗等级
5. ✅ 唤醒后恢复：时钟 → 引脚 AF 模式 → UART 重初始化 → ISR 重配置

**潜在漏电路径**：
- GPIO 初始状态：`Battery_CHK_Pin(PC8)` = OUTPUT PP LOW → 会持续消耗 ~200 µA（分压电阻）
- `RESET_N_Pin(PC2)` = OUTPUT PP LOW → 4G 模块 RESET 低电平，但模块已断电，无影响
- 未使用引脚较多：建议确认所有未使用 GPIO 在 STOP 前设为模拟模式
- 建议检查 `MX_GPIO_Init()` 中配置的所有引脚是否在 `pm_enter_stop()` 中都被处理

---

### 4. W25Q16JV (Winbond 2MB SPI NOR Flash)

| 模式 | 典型电流 | 进入方式 |
|------|----------|----------|
| **Deep Power Down (DPD)** | **< 1 µA** | 发送 0xB9 指令 |
| Standby (CS=HIGH) | ~5 µA typ | 默认状态，CS 拉高即可 |
| Active Read | up to 25 mA | — |
| Program/Erase | up to 25 mA | — |

**当前实现** (详见 `flash_io.c`):
- `sFLASH_CS_HIGH()` 在每次操作后执行：CS=HIGH, HOLD=LOW, MISO→模拟模式
- 休眠期间：SPI1 时钟关闭 (`pm_enter_stop()` 中 `__HAL_RCC_SPI1_CLK_DISABLE()`)
- 当前未发送 DPD 命令 → 休眠电流 ~5 µA (standby)

**优化空间 (可节省 ~4 µA)**：
1. 在 `pm_enter_stop()` 前发送 DPD 命令 (0xB9)
2. 唤醒后在需要时发送 Release DPD 命令 (0xAB)
3. 实现：在 `pm_enter_stop()` 的时钟关闭之前调用新增的 `sFLASH_EnterDeepPowerDown()`
4. 唤醒后在 `pm_enter_stop()` 末尾或者 `sFLASH_CS_LOW()` 前发送 Release DPD

**注意**：DPD 模式下的唯一有效命令是 Release DPD (0xAB)，其他所有指令（包括读状态寄存器）都会被忽略。

---

### 5. MP4560DN (MPS DC-DC 降压转换器，电池→4V)

| 模式 | 典型电流 | 条件 |
|------|----------|------|
| **Shutdown (EN<1.2V)** | **~12 µA** | CTL_4V=0V |
| Quiescent (EN=HIGH, 空载) | ~140 µA | CTL_4V=HIGH |
| Active (带载) | 140µA + 负载电流 | 取决于后级负载 |

**当前实现**：
- 休眠时 CTL_4V(PB13)=LOW → EN < 1.2V → MP4560DN 进入 shutdown
- **注意**：EN 引脚有内部 1µA pull-up 到 ~3.0V → 要可靠拉低需 GPIO 灌电流 > 1µA
- Shutdown 电流 12µA 会一直消耗（因电池始终连接到 VIN）
- 这是**不可避免的静态功耗**

**配置检查**：
- 当前 `GPIO_InitStruct.Pull = GPIO_NOPULL` → 休眠时 GPIO 输出 LOW，内部 NMOS 下拉提供低阻抗路径
- 确认在 `pm_enter_stop()` 中 PB13 保持输出 LOW 状态，未被改为模拟模式

---

### 6. MD5718 (ETEK 双通道负载开关)

| 模式 | 典型电流 | 条件 |
|------|----------|------|
| **关断 (IS_VBIAS)** | **~1.0 µA** | VBIAS 仍在供电，但 VIN 无电 |
| 静态 (双通道使能) | ~22 µA | 2 通道均开启 |
| 导通电阻 RDS(on) | 18 mΩ typ | VIN=5V, IOUT=0.2A |

芯片规格：
- 双通道独立负载开关
- 最大持续电流：6A/通道
- VIN: 0.6V ~ VBIAS
- VBIAS: 2.5V ~ 5.7V
- 关断电流 IS_VBIAS: 1.0 µA

**当前实现**：
- CTL_4VOE(PB14) 在上电和休眠期间都保持 LOW
- 上电期间：PB14=LOW（可能为 active-low enable → 负载开关导通）
- 休眠期间：PB14=LOW，但上游 MP4560DN 已关断 → VIN=0V → MD5718 实际消耗仅 IS_VBIAS ~1 µA

**不确定项** (需硬件确认)：
- MD5718 的 EN 引脚逻辑 (active HIGH 还是 active LOW)
- VBIAS 电源来源（电池直连还是另有 LDO）
- 为什么启动时 CTL_4VOE = LOW 而非 HIGH

---

### 7. KT6328A (清月电子 BLE 透传芯片)

| 模式 | 典型电流 | 备注 |
|------|----------|------|
| 开机瞬间 | ~15 mA | 持续 ~200ms |
| **未连接 - 广播 (500ms 间隔)** | **~185 µA avg** | 100ms 广播 + 400ms 睡眠交替 |
| 最低睡眠电流 | ~20 µA | — |
| 已连接保持 | ~4.3 mA | — |
| 完全禁用 (如果支持) | ~0-2 µA | 需确认是否有 POWER_DOWN 指令 |

**当前实现**：
- BLE 芯片在代码中仅配置了 BLE_INT_Pin(PB3) 为输入上拉
- BLE_TXD(PA9), BLE_RXD(PA10) 未在初始化中配置（可能由其他代码配置或未使用）
- 休眠期间：BLE 芯片似乎**没有被主动关闭** → 持续以 ~185 µA 广播

**❗重要发现 — 主要漏电源**：
- 如果 KT6328A 在休眠期间继续以广播模式运行，会消耗 ~185 µA
- 这远高于 STM32 STOP 模式的 ~1.5 µA，会成为整个系统休眠功耗的**最大来源**
- 建议：
  1. 通过 AT 指令让 KT6328A 进入 deep sleep（需要查阅其 AT 指令集）
  2. 或通过 GPIO 控制其电源 / RST 引脚
  3. 或通过 UART 发送 sleep 指令后等待确认

**GPIO 引脚**：
- BLE_TXD: PA9 (UART1_TX) — 若 UART1 使用了此引脚，可能与 BLE 共享
- BLE_RXD: PA10 (UART1_RX) — 同上
- BLE_INT: PB3 (输入上拉)

---

## 休眠功耗预算 (Sleep Budget) — 修订版

基于上述分析，每个休眠周期的功耗构成：

| 模块 | 当前实现 | 优化后可达 |
|------|----------|------------|
| STM32L152 (STOP+LSI RTC) | ~1.5 µA | ~1.5 µA |
| EC800E 4G | **0 µA** (完全断电) ✅ | 0 µA |
| TXS0108E | **接近 0 µA** (VCCB 断电) ✅ | 0 µA |
| W25Q16 (Standby) | ~5 µA | **~1 µA** (启用 DPD) |
| MP4560DN (Shutdown) | ~12 µA | ~12 µA (无法消除) |
| MD5718 | ~1 µA (估算) | ~1 µA |
| KT6328A BLE | **~185 µA** ⚠️ | **~2-20 µA** (睡眠模式) |
| GPIO 漏电 + 其他 | ~3 µA | ~3 µA |
| **合计** | **~208 µA** | **~39 µA** |

**关键结论**：
- 当前休眠功耗 ~208 µA，其中 **KT6328A BLE 占 ~89%**
- 优化 BLE 进入睡眠后，休眠功耗可降至 ~23-41 µA
- 进一步优化 W25Q16 进入 DPD 可再节省 ~4 µA
- MP4560DN 的 12 µA shutdown 电流是不可避免的硬件限制（电池直连）

---

## 优化优先级 (ROI 排序)

| 优先级 | 优化项 | 当前 | 目标 | 节省 |
|--------|--------|------|------|------|
| **🔴 P0** | KT6328A BLE 进入 deep sleep | ~185 µA | ~2-20 µA | **~165-183 µA** |
| **🟡 P1** | W25Q16 发送 DPD 命令 | ~5 µA | ~1 µA | **~4 µA** |
| **🟢 P2** | 未用 GPIO 全部设为模拟模式 | ~3 µA | ~1 µA | ~2 µA |
| **🔵 P3** | MP4560DN 考虑外部 MOSFET 切断 | ~12 µA | 0 µA | ~12 µA (需改 PCB) |

---

## GPIO 控制矩阵 (休眠时状态)

| GPIO | 标签 | 休眠状态 | 控制模块 |
|------|------|----------|----------|
| PB13 | CTL_4V | OUTPUT LOW | MP4560DN EN → shutdown |
| PB14 | CTL_4VOE | OUTPUT LOW | MD5718 EN (active-low?) |
| PB1 | CTL_1V8 | OUTPUT LOW | 1.8V 电源关断 |
| PC13 | VCC_DRV | OUTPUT LOW | TXS0108E VCCB 断电 |
| PA4 | OE_TXS0108 | OUTPUT LOW | TXS0108E OE=disable |
| PH1 | PWRKEY | OUTPUT LOW | 4G 模块 PWRKEY |
| PB2 | DTR | OUTPUT LOW | 4G 模块 DTR |
| PC2 | RESET_N | OUTPUT LOW | 4G 模块 RESET |
| PA2/PA3 | USART2 TX/RX | **ANALOG** | 消除漏电 |
| PC10/PC11 | USART3 TX/RX | **ANALOG** | 消除漏电 |
| PA5/PA6/PA7 | SPI1 SCK/MISO/MOSI | **ANALOG** | 消除漏电 |
| PA6 | SPI1 MISO | **ANALOG** (sFLASH_CS_HIGH 调用) | 消除漏电 + 总线隔离 |
| PB10 | FLASH_HOLD | OUTPUT **LOW** | W25Q16 HOLD (sFLASH_CS_HIGH 设置) |
| PB11 | FLASH_NSS | OUTPUT **HIGH** | W25Q16 CS 拉高 (deselect) |
| PC8 | Battery_CHK | **OUTPUT LOW** ⚠️ | 分压电阻持续耗电 (~200µA) |
| PC12 | LED | OUTPUT LOW | LED 熄灭 |

### ⚠️ Battery_CHK 问题

`Battery_CHK_Pin(PC8)` 初始化为 `OUTPUT PP LOW`，用于控制电池电压检测的分压电阻接地端。如果该引脚的上拉/分压电阻网络在 LOW 时仍有电流路径，会产生额外功耗。

在 `pm_enter_stop()` 中**未将 PC8 设为模拟模式** → 可能通过分压电阻消耗 ~200 µA（取决于分压电阻值）。

**建议**：在 `pm_enter_stop()` 中将 PC8 设为模拟模式 (GPIO_MODE_ANALOG)，唤醒后恢复为输出。

---

## 内存索引

- [[pm-state-machine-flow]] — PM 状态机完整流程
- [[gpio-power-control-matrix]] — GPIO 控制详细矩阵
- [[ble-kt6328a-integration]] — BLE 模块状态

**Why:** 全面梳理 7 个模块在休眠期间各自的低功耗模式和实际电流消耗，为功耗优化提供数据支撑和优先排序。
**How to apply:** 按优化优先级顺序实施，首先解决 KT6328A BLE 漏电问题，其次 W25Q16 DPD，最后检查 GPIO 漏电路径。
