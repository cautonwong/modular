# InfiniTime 智能手表固件迁移 — 任务、判据与推进路线（精细版）

> 上游参照：`/workspaces/vendor/InfiniTime`（PineTime 智能手表固件）。
> 决策源 [`adr.md`](adr.md)（D1-D85）；实现差异**只**记录在 [`adr-conformance.md`](adr-conformance.md)。
> 本文是**基于 GitNexus 知识图谱的 1:1 语义对齐规格书、算法细节与分阶段推进路线**。

---

## 1. 任务背景与核心架构铁律

将 `vendor/InfiniTime`（基于 C++14、FreeRTOS、NimBLE、LittlevGL 架构的 PineTime 智能手表固件）按**语义一比一**迁入本工程的 8 层 C11 嵌入式架构，彻底满足三条核心铁律：
1. **消费者定义端口（Consumer-Defined Ports, D14）**：驱动与应用解耦，应用拥有接口签名与 `void *self` 显式持有指针；
2. **状态聚合根与纯标量事件（Aggregate Root & Scalar Events, D66）**：输入/传感器/电源等聚合根持有内部状态机与不变量，跨模块通信走 24 字节标量 `edge_event_t` 与 Payload Token 机制；
3. **组合根显式装配与运行期零动态内存（Composition Roots & Zero Runtime Allocation, D5/D6/D7/D21）**：禁止任何运行期 `malloc`/`free`/`new`/`delete`，所有状态、任务栈、消息队列在 `product/` 显式静态装配。

---

## 2. 核心子系统 1:1 语义细节与算法规范

### 2.1 触控与手势识别（Touch & Gesture）
- **上游源码**：`src/drivers/Cst816s.cpp`、`src/touchhandler/TouchHandler.cpp`
- **目标模块**：`infra/cst816s`（驱动解码）+ `app/touch_gesture`（交互状态机）
- **1:1 语义与协议规范**：
  1. **I2C 报文解析**：从从机地址 `0x15` 读取偏移 `0x01` 处的 6 字节数据：
     - `raw[0]`: 硬件手势识别码（`0x01` 下滑、`0x02` 上滑、`0x03` 左滑、`0x04` 右滑、`0x05` 单击、`0x0B` 双击、`0x0C` 长按）；
     - `raw[1]`: 触控点数（低 4 位，`0` 或 `1`）；
     - `raw[2] & 0x0F`: X 轴高 4 位，`raw[3]`: X 轴低 8 位 $\to X = (X_{high} \ll 8) | X_{low}$；
     - `raw[4] & 0x0F`: Y 轴高 4 位，`raw[5]`: Y 轴低 8 位 $\to Y = (Y_{high} \ll 8) | Y_{low}$。
  2. **坐标边界与异常保护**：$X \ge 240$ 或 $Y \ge 240$ 判定为无效数据，拒绝越界事件。
  3. **单触控防重复触发锁（Continuous Touch Lock）**：滑动（Slide）与长按（LongPress）手势在手指未抬起（`touching == true`）期间只触发一次，必须待 `touching == false`（手指抬起）将 `gesture_released` 置为 `true` 后才允许触发下一手势。

---

### 2.2 电池电量与电源生命周期（Battery & Power State Machine）
- **上游源码**：`src/components/battery/BatteryController.cpp`、`src/systemtask/SystemTask.cpp`
- **目标模块**：`infra/battery_adc`（采样与电量转换）+ `app/watch_power`（电源生命周期）+ `sys/watch`（家族调度策略）
- **1:1 语义与算法规范**：
  1. **SAADC 电压换算**：PineTime 采用 $1/2$ 硬件分压电阻，nRF52832 SAADC 增益配置为 $1/4$，内部参考基准 $600\text{ mV}$，10-bit ADC（1024 满量程）：
     $$V_{\text{bat}} (\text{mV}) = \text{raw\_adc} \times \frac{8 \times 600}{1024} = \text{raw\_adc} \times \frac{75}{16}$$
  2. **6 点非线性放电折线插值模型**：
     $$\text{LUT} = \{(3500\text{mV}, 0\%), (3616\text{mV}, 3\%), (3723\text{mV}, 22\%), (3776\text{mV}, 48\%), (3979\text{mV}, 79\%), (4180\text{mV}, 100\%)\}$$
  3. **充放电滤波与迟滞**：
     - 未充满充电状态下，电量百分比钳位在 $99\%$；仅在电源接通且充电引脚指示停止（`is_full = true`）时置 $100\%$；
     - 放电期间执行单调滤波：计算百分比若高于当前记录值则不予递增，避免瞬时负载回弹造成跳电。
  4. **四状态电源生命周期**：
     - **AWAKE（全功能亮屏）**：屏幕开启、背光 $100\%$，若 10 秒无交互则进入 `DIMMED`；
     - **DIMMED（渐暗过渡）**：背光降至 $10\%$，若 5 秒内无交互则进入 `SLEEPING`；若发生触控/按键立即恢复 `AWAKE`；
     - **SLEEPING（深度休眠）**：关闭 LCD 显存、触控与心率进入低功耗待机，CPU 挂起进入 Tickless WFI；
     - **CHARGING（充电指示）**：充电器接入时唤醒屏幕，显示充电动画，充满后保持微功耗指示。

---

### 2.3 运动检测与手腕抬起唤醒（Motion & Wrist Tilt Wake）
- **上游源码**：`src/drivers/Bma421.cpp`、`src/components/motion/MotionController.cpp`
- **目标模块**：`infra/bma421`（I2C 加速度驱动）+ `app/step_counter`（计步与手势）
- **1:1 语义与算法规范**：
  1. **手腕抬起唤醒算法 (`ShouldRaiseWake`)**：
     - 维护 16 点滑动历史三轴加速度采样窗口（采样率 10Hz）；
     - 计算 $X, Y, Z$ 均值与方差，重力方差阈值 $\text{varianceThresh} = 56 \times 56 = 3136$；
     - $X$ 轴倾角阈值 $|X_{\text{mean}}| \le 384$，$Y$ 轴倾角阈值 $Y_{\text{mean}} \le -64$；
     - 通过反正弦函数计算翻腕角：$\text{DegreesRolled}(Y_{\text{mean}}, Z_{\text{mean}}, Y_{\text{prev}}, Z_{\text{prev}}) < -45^\circ$ 时判定为抬腕亮屏，触发 `EDGE_EVT_WATCH_WRIST_WAKE`。
  2. **下垂手腕熄屏算法 (`ShouldLowerSleep`)**：
     - 翻腕角 $> +30^\circ$ 或手臂垂下（$Y$ 轴重力分量持续占主导）时触发熄屏。
  3. **计步累加与跨天结算 (`AdvanceDay`)**：
     - 每日午夜（`EDGE_EVT_WATCH_TIME_DAY`）自动保存当日步数历史并清零当前步数计数器。

---

### 2.4 心率采样与 PPG 信号滤波（PPG Heart Rate Algorithm）
- **上游源码**：`src/drivers/Hrs3300.cpp`、`src/components/heartrate/Ppg.cpp`、`HeartRateTask.cpp`
- **目标模块**：`infra/hrs3300`（光电传感器驱动）+ `app/heart_rate`（PPG 数字信号处理）
- **1:1 语义与算法规范**：
  1. **传感器配置与双通道采集**：
     - LED 驱动电流配置为 `0x2F`（12.5mA 稳态电流）；
     - 采样周期 $100\text{ ms}$（10Hz 采样率），同步读取环境光通道（ALS）与 PPG 通道数据。
  2. **信号去趋势与带通滤波**：
     - **去趋势（Detrend）**：扣除窗口起点与终点的一次线性漂移斜率，消除基线漂移；
     - **0.5Hz~4.0Hz 双级指数带通滤波（Filter30to240）**：
       - 高频衰减（$\alpha = 0.816 \approx 4\text{ Hz}$）消除高频肌电与工频噪声；
       - 低频扣除（$\alpha = 0.268 \approx 0.5\text{ Hz}$）消除呼吸与低速晃动。
  3. **窗函数与频谱能量分析**：
     - 施加 64 点对称 Hanning 窗系数消除频谱泄漏；
     - 64 点 FFT 变换提取功率谱，计算心率感兴趣区域（30~240 BPM）的信噪比（$\text{SNR} > \text{Threshold}$）；
     - 抛物线高斯波峰插值质心搜索（`PeakSearch`），精确计算心率波峰频率并换算为即时 BPM。
  4. **佩戴脱落检测**：
     - 当环境光溢出 $ALS > ALS_{\text{threshold}}$（手表脱离手腕）时，立即置心率无效（0 BPM）并停止无谓发光。

---

### 2.5 时间日历与多定时器管理（DateTime & Time Engine）
- **上游源码**：`src/components/datetime/DateTimeController.cpp`、`AlarmController.cpp`、`StopWatchController.cpp`
- **目标模块**：`app/watch_time`
- **1:1 语义规范**：
  1. **RTC1 24-bit 计数器无损时间累加**：
     - 跟踪 nRF52 RTC1 计数器（$32768\text{ Hz}$ / 预分频后 $1024\text{ Hz}$），处理 `0x00FFFFFF` 计数溢出翻转；
     - 高精度推导公历纪年（闰年判定：`(year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)`），输出年月日、时分秒、星期。
  2. **时钟周期性事件发生器**：
     - 整点（`minute == 0`）产生 `EDGE_EVT_WATCH_TIME_HOUR`；
     - 半点（`minute == 30`）产生 `EDGE_EVT_WATCH_TIME_HALF_HOUR`；
     - 午夜（`hour == 0`）产生 `EDGE_EVT_WATCH_TIME_DAY`。
  3. **闹钟、秒表、倒计时状态机**：
     - 纯整数无浮点毫秒/秒/分累加器，支持分段计时（Lap）与到达触发。

---

### 2.6 BLE GATT 服务协议（BLE GATT Services）
- **上游源码**：`src/components/ble/CurrentTimeService.cpp`、`HeartRateService.cpp`、`BatteryInformationService.cpp`、`AlertNotificationService.cpp`
- **目标模块**：`app/ble_services`
- **1:1 线格式与二进制协议对齐**：
  1. **CTS 当前时间服务（UUID: 0x1805 / 0x2A2B）**：
     - 10 字节标准二进制格式：`Year (uint16_t, Little-Endian) | Month (uint8_t, 1-12) | Day (uint8_t, 1-31) | Hours (uint8_t, 0-23) | Minutes (uint8_t, 0-59) | Seconds (uint8_t, 0-59) | DayOfWeek (uint8_t, 1=Mon..7=Sun) | Fractions256 (uint8_t) | AdjustReason (uint8_t)`。
  2. **HRS 心率服务（UUID: 0x180D / 0x2A37）**：
     - 标量 GATT 广播帧：`Flags (0x00: 8-bit BPM, 传感器接触良好) | HeartRateValue (uint8_t)`。
  3. **BAS 电池服务（UUID: 0x180F / 0x2A19）**：
     - 单字节电量：`BatteryLevel (uint8_t, 0-100)`。
  4. **ANS 提醒通知服务（UUID: 0x1811 / 0x2A46）**：
     - 解析 2 字节头 + UTF-8 变长文本：`CategoryID (uint8_t: 0=Email, 1=News, 2=Call, 3=MissedCall, 4=SMS) | Count (uint8_t) | Message Payload`。

---

## 3. 消费者定义接口声明（Consumer-Defined Ports）

严格满足 **D14 消费者定义** 与 **零动态分配** 原则，接口由 App 声明，底层在 `glue.c` 中显式适配：

```c
/* 触控输入消费者端口 */
typedef struct touch_input_if {
    edge_status_t (*read_touch)(void *self, touch_raw_info_t *out_info);
    edge_status_t (*sleep)(void *self, bool enable);
    void *self;
} touch_input_if_t;

/* 显示与背光消费者端口 */
typedef struct display_port {
    edge_status_t (*set_window)(void *self, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
    edge_status_t (*write_pixels)(void *self, const uint16_t *colors, size_t count);
    edge_status_t (*set_brightness)(void *self, uint8_t level_percent);
    edge_status_t (*sleep)(void *self, bool enable);
    void *self;
} display_port_t;

/* 惯性与计步消费者端口 */
typedef struct imu_port {
    edge_status_t (*read_steps)(void *self, uint32_t *steps);
    edge_status_t (*read_accel)(void *self, int16_t *x, int16_t *y, int16_t *z);
    void *self;
} imu_port_t;

/* 光学心率采集消费者端口 */
typedef struct ppg_port {
    edge_status_t (*read_sample)(void *self, uint16_t *als, uint16_t *ppg);
    edge_status_t (*enable)(void *self, bool enable);
    void *self;
} ppg_port_t;

/* 电池电量采样消费者端口 */
typedef struct battery_port {
    edge_status_t (*read_voltage_mv)(void *self, uint16_t *voltage_mv);
    edge_status_t (*is_charging)(void *self, bool *charging);
    edge_status_t (*is_power_present)(void *self, bool *present);
    void *self;
} battery_port_t;
```

---

## 4. 分阶段精细推进路线图与验证矩阵

```
Phase 0: 核心算法与离线解码器（已交付）
  ├─ infra/battery_adc (SAADC 毫伏换算与 6 点折线放电模型)
  ├─ infra/cst816s (CST816S 6 字节报文解析与边界校验)
  └─ app/touch_gesture (手势连续触控锁与状态机)
    ↓
Phase 1: 系统调度、电源与按键交互
  ├─ sys/watch (智能手表族调度策略与事件预算 SYS_WATCH_EVENT_BUDGET=8)
  ├─ app/watch_power (AWAKE / DIMMED / SLEEPING / CHARGING 四状态机)
  └─ app/button_handler (物理按键 50ms 消抖与 2.5s 长按关机判定)
    ↓
Phase 2: 健康监测与时钟日历
  ├─ infra/hrs3300 + app/heart_rate (PPG 0.5~4Hz 带通滤波与波峰心率算法)
  ├─ infra/bma421 + app/step_counter (手腕抬起 -45° 唤醒与计步统计)
  └─ app/watch_time (RTC1 计数累加、公历算法与整点/半点事件)
    ↓
Phase 3: 硬件底层与板级绑定
  ├─ infra/st7789 (ST7789 240x240 SPI 刷屏驱动与背光控制)
  ├─ infra/flash_spi (XT25F32B SPI Flash 页读写与扇区擦除)
  ├─ infra/haptic (震动马达短促/长震时序生成)
  └─ board/pinetime (PineTime nRF52832 引脚配置与 GPIOTE 中断推入 Event Sink)
    ↓
Phase 4: BLE 协议解析与服务通信
  └─ app/ble_services (CTS 10字节时间包、HRS 心率包、BAS 电池包、ANS 消息提醒解析)
    ↓
Phase 5: 视图状态机与产品组合根
  ├─ app/watch_ui (事件驱动的应用与表盘路由状态机)
  ├─ product/watch_host (Linux 宿主机无头模拟器与全功能差分测试)
  └─ product/pinetime (PineTime 硬件组合根，全量 0 堆内存分配验证)
```

---

## 5. 完成判据与门禁

| 检查维度 | 工具 / 判据 | 目标状态 |
|---|---|---|
| **单元测试** | `ctest --test-dir build --output-on-failure` | 100% 通过（当前 60/60 PASS） |
| **消费者接口** | `python3 .github/scripts/check_consumer_ports.py` | 0 违规，所有端口带 `void *self` |
| **8 层依赖** | `python3 .github/scripts/check_layer_dependencies.py` | 0 越界，禁止应用层直连底层驱动头文件 |
| **动态内存扫描** | `python3 .github/scripts/check_no_dynamic_memory.py <elf>` | 0 动态分配符号（0 `malloc`/`free`） |
| **模块与事件 ID** | `check_module_ids.py` / `check_event_ids.py` | 全量在分配段内严格对齐 |
| **代码格式** | `clang-format --dry-run -Werror` | 100% 遵循工程统一代码风格 |
