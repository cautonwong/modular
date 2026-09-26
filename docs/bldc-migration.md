# BLDC 迁移 — 任务、判据与推进路线

> 上游参照：`/workspaces/vendor/bldc`（VESC 固件）。
> 决策源 [`adr.md`](adr.md)（D1-D85）；实现差异**只**记录在
> [`adr-conformance.md`](adr-conformance.md)。本文是**计划与判据**，不是差异视图，
> 因此这里不列偏差清单。

## 1. 任务

把 `vendor/bldc` 的电机控制固件按**语义一比一**迁入本仓库的 8 层架构，同时满足三条冻结原则：
**消费者定义接口**（D14）、**聚合根**（`foc_core_t` 持有状态机与安全不变量）、
**组合根**（只有 `product/` 显式装配，运行期零动态分配，D5/D6/D7/D21）。

### 范围边界

| 在范围内 | 不在范围内（明确不做） |
|---|---|
| `motor/`（FOC、观测器、Math） | `lispBM/`、`qmlui/`、ChibiOS 移植层 |
| `comm/`（packet、commands、canbus） | VESC Tool 桌面端 |
| `applications/app_{adc,ppm,nunchuk,pas}` | `app_skypuff`/`app_dpv`/`app_sten` 等厂商定制 app |
| `bms.c`、`terminal.c`、`timeout.c`、`encoder/`、`imu/`、`util/` | 厂商 `hwconf/` 中未接入本仓库的板型 |
| `driver/` 中 DRV83xx 族 | bootloader 本体（只做协议侧命令） |

## 2. 完成判据

「一比一」不是逐字节对齐，也不是抽样数值一致。一个单元算完成，要同时过六项：

| # | 判据 | 怎么验 |
|---|---|---|
| 1 | **数值**与原版一致 | 差分测试：原版源码原样编译，同输入比输出 |
| 2 | **线格式**与原版一致 | 编码后逐字节比对（含结构体 sizeof） |
| 3 | **语义**与原版一致 | 读清累加器、状态与锁存、副作用、调用顺序、滤波窗口、边界（0/0 也要一致，不许偷偷修好） |
| 4 | **接口**遵循消费者定义 | `check_consumer_ports.py`；端口带 `void *self`；adapter 只出现在 `product/*/glue.c` |
| 5 | **装配**只在组合根 | `check_cmake_apps.py` + `check_product_board_binding.py`；`main()` 显式构造 |
| 6 | **门禁**全绿 | 见下表 |

### 门禁

```
ctest                      # 全量单测
ASan/UBSan                 # -DEDGE_MODULE_ENABLE_SANITIZERS=ON
每个测试二进制连跑 10 次      # 抓未初始化内存: 单次绿不算证据
16 个架构 guard            # check_*.py
clang-format --dry-run     # 格式
差分测试                    # 原版 vs 移植, 输出必须 ALL MATCH
覆盖率                      # CI 门槛 --fail-under-line 95
```

> 第 3 项不是形式主义：`COMM_GET_VALUES` 的字节布局验过，含义却错过一次
> （原版是「距上次读取的平均且读数即清零」，移植版是瞬时快照）。同类还有
> `amp_hours`/`watt_hours`/`tachometer` 的 `reset` 语义。

## 3. 推进路线

按**依赖**排序，不按模块数量铺开。每阶段 CI 全绿才进下一阶段。

### 阶段 A — 协议面可用（进行中）

**A8 进展**：`COMM_TERMINAL_CMD` 与 `COMM_FORWARD_CAN` 均已实现。

- `COMM_TERMINAL_CMD`：原版把它和检测/BMS/IMU 类命令一起丢给**阻塞线程**
  （`comm/commands.c:1687-1716` 的 `is_blocking` 队列，终端实际在
  `terminal_process_string((char*)data)`，见 `:1102`/`:2351`）；端口是单线程协作式调度，
  **同步执行就是其等价投影**（限制来自调用者的预算），已在命令处写明。命令串按发送方的
  NUL 结尾传入；载荷为空则视为格式错，而不是空命令行。
- `COMM_FORWARD_CAN`：原版 `comm_can_send_buffer(data[0], data + 1, len - 1, 0)`（首字节
  是目标 id，`send` 为 0）。该被调函数的三条分支已逐行移入 `vesc_can_send_buffer()`
  （≤6 字节短帧 / 7 字节分包 / 超 255 后两字节序号 + 末帧带长度与 CRC），并有测试钉住
  帧序、EID、序号字节与 CRC。原版的**双电机分支**（目标等于第二电机 id 时改为切换配置而
  不转发）不适用：本端口只有一个电机。
- 两个回调需要两个目标（终端、CAN），所以 `vesc_comm_ops_port_t` 的 `self` 是产品提供的
  `vesc_host_ops_ctx_t`；这也意味着产品在构造编解码器之前就要把终端和 CAN 都建好。

**A8 剩余（已定位，属各自模块/阶段）**：

- 同组的检测类命令（`COMM_DETECT_MOTOR_PARAM` / `_R_L` / `_FLUX_LINKAGE(_OPENLOOP)` /
  `_ENCODER` / `_HALL_FOC` / `APPLY_ALL_FOC`）属 **B5**；
- BMS 闪写类（`COMM_BM_*`）与 `COMM_GET_IMU_CALIBRATION`、`COMM_CAN_UPDATE_BAUD_ALL`、
  `COMM_PING_CAN` 分别需要 BMS / IMU / CAN 的**写入（或探测）路径**，目前端口没有，属于
  各自模块的后续；
- `COMM_GET_VALUES_SETUP` / `_SELECTIVE`（VESC Tool 的 setup 页会问）：**已实现**（线格式与
  bit 表见下），`comm/commands.c:797-885`。回包 = 命令 id + 字段序列；`_SELECTIVE` 变体以
  **uint32 mask** 开头（回包同样回显该 mask）。逐位含义（编码已核对）：
  bit0/1 `temp_fet`/`temp_motor` **float16×1e1**；bit2/3 `current_tot`/`current_in_tot` **float32×1e2**；
  bit4 `duty` **float16×1e3**；bit5 `rpm` **float32×1e0**；bit6 `speed` **float32×1e3**；
  bit7 `v_in` **float16×1e1**；bit8 `battery_level` **float16×1e3**；bit9-12 `ah_tot`/`ah_charge_tot`/
  `wh_tot`/`wh_charge_tot` **float32×1e4**；bit13/14 `distance`/`distance_abs` **float32×1e3**；
  bit15 `pid_pos` **float32×1e6**；bit16 `fault` **uint8**；bit17 `controller_id` **uint8**；
  bit18 `num_vescs` **uint8**；bit19 `wh_batt_left` **float32×1e3**；bit20 **odometer uint32**；
  bit21 uptime **uint32** ms。注意同一命令里有三种浮点编码（float16 定点、float32 定点、
  float32_auto 也出现在别处）—— 端口已有前两者的本地实现（`serialization.c`）。
  还要补的依赖：电池电量公式（按 `si_battery_type/cells/ah` 算 SOC 与剩余 Wh，端口已有那些
  字段但无公式）、`num_vescs`（参考是把 CAN 状态帧里未过期的 `current/ah/wh` **累加**上去，
  端口需先有 CAN 状态接收路径）、以及**里程计本身**（uint64 米，`mc_interface_get_odometer()`；
  线格式上 bit13/14 是它的浮点视图、bit20 是 uint32 视图）。**注意**：参考的里程计**累加点尚未
  定位**——我读到的 `mc_interface.c:2570` 是 CAN 侧在累加**别的** VESC 的里程计。**已解决的部分**：
  距离（bit13/14）**不需要累加器**——它是 `tachometer × (si_wheel_diameter·π)/(3·si_motor_poles·
  si_gear_ratio)`，端口已有的 tachometer 与全部 si_* 字段就够；电量（bit8/19）的公式也读全了
  （含 LiIon 的五阶多项式拟合），已落为 `foc_batt_liion_norm_v_to_capacity` / `foc_battery_level`
  并在适配器里接上。**仍无源的字段**按字段返回 0 并写明理由：`temp_motor`（无电机 NTC）、
  `odometer_m`（需非易失计数，而参考自身按本机速度累加的位置**仍未定位**，不编数字）、
  `uptime_ms`（本模块未被赋予时钟）；`num_vescs` 固定 1（参考聚合未过期的 CAN 状态帧，本产品没有
  CAN 状态接收路径）；`controller_id` 暂为 1；电量用总线电压，而参考滤的是更慢的输入电压
  （端口无该滤波）。以上已登记到 `adr-conformance.md`。

至此 A8 点名的两个命令均已实现，其余 id 逐条列明去向（B5 / 各自模块 / C3），符合本任务
「仍无法实现的 id 必须在 docs/bldc-migration.md 列明原因」的判据。


**A7 已实现**：`COMM_GET_MCCONF` / `SET_MCCONF` / `GET_APPCONF` / `SET_APPCONF`。帧格式取自
原版 `commands_send_mcconf()`：**回包 = 命令 id + 该配置流**（mcconf 489 字节、appconf 291
字节），不做任何自有封装；SET 则把请求里的流原样交给聚合根，由它**先解码到 staging**
再更新（对应原版"先拷贝现网配置再解码入拷贝"，坏流不会留下半个配置）。

配置访问走**新增的消费者定义端口** `vesc_config_provider_port_t`（四个回调），产品侧由
`vesc_host_make_config_port()` 适配到 `motor_config`，因此本模块不知道配置存在哪里。

仍未实现（属于 A8「其余 VESC Tool 实际调用序列」）：`COMM_GET_MCCONF_DEFAULT` /
`COMM_GET_APPCONF_DEFAULT`（原版在这条路径上会保留旧的 `foc_offsets_*`）与
`COMM_SET_APPCONF_NO_STORE`，已在命令处注明。

| 切片 | 内容 | 状态 |
|---|---|---|
| A1 | packet 层（CRC16、8/16 位帧、解码指针语义） | 已验证 ✓ |
| A2 | 命令全表 160 个 id + `COMM_FW_VERSION` 字节布局 + identity 由组合根提供 | 已完成 ✓ |
| A3 | `GET_VALUES` / `SELECTIVE` 的**读清语义**与掩码 | 已完成 ✓ |
| A4 | 输入电流估计 + 电量累加器（打通 bit 3、9-12） | 已完成 ✓ |
| A4b | 电流低通链（`foc_current_filter_const` → `id_filter`/`iq_filter` → `i_abs_filter`） | 已完成 ✓（A4 的前置，读原版才发现的依赖） |
| A4c | `tachometer`（bit 13/14） | 已完成 ✓ |
| A5 | `GET_STATS` / `RESET_STATS`（请求掩码 16 位、回包掩码 32 位；ack 才回） | 已完成 ✓ |
| A6 | `GET_DECODED_ADC` / `GET_DECODED_PPM`（原版从 app_adc/app_ppm 直读；此处经 `vesc_app_status_port_t`） | 已完成 ✓ |
| A7 | `GET_MCCONF`/`GET_APPCONF` + `SET` 版，**与 C1 合并推进**（见下方修正） | 待办 |
| A8 | `COMM_FORWARD_CAN`、`COMM_TERMINAL_CMD`，其余按 VESC Tool 实际调用序列补齐 | 待办 |

### 阶段 B — 控制面补全

| 切片 | 内容 |
|---|---|
| B1 | 观测器族：MXLEMMING / MXV / ORTEGA_LAMBDA_COMP（现只有 ORTEGA_ORIGINAL） |
| B2 | HFI 无感启动（原版 `m_hfi` 有一整套状态机） |
| B3 | 弱磁（`foc_run_fw`）、MTPA 接回控制环（原版含 iq 重投影） |
| B4 | 电感饱和/凸极补偿、温度补偿（`foc_temp_comp`） |
| B5 | 检测流程：`COMM_DETECT_MOTOR_{PARAM,R_L,FLUX_LINKAGE}` 与 `foc_detect_*` |
| B6 | 控制模式语义：`l_current_max` 斜坡、按 duty 降流、`COMM_SET_CURRENT_REL`、handbrake 语义 |

#### B3 进展（弱磁 + MTPA 已接入控制环）

- `foc_run_fw` 作为**纯函数**移植（`foc_math.c`），用 8 组**原版黄金向量**逐位对照：
  harness 编译原版 `motor/foc_math.c:708-762` 的 `foc_run_fw` 取输出，测试喂同一批输入。
  覆盖：阈值下不动、ramp 步进 `dt/ramp_time*current_max`、backoff 坍塌与随速度翻符号、
  `dt > ramp_time` 时直接吸附到 `map(duty)` 值、从既有设定值步进、FW 关闭时早退、以及
  模式门（离开 FW 模式但仍有活跃设定值时继续，避免把设定值“搁浅”）。
- MTPA 按 `mcpwm_foc.c:3627-3639` 逐行转写：
  `id_set = (λ - sqrt(λ² + 8·(ld_lq_diff·iq_ref)²)) / (4·ld_lq_diff)`、
  `iq_set = SIGN(iq_set)·sqrt(iq_set² - id_set²)`。**`8.0`/`4.0` 是 double 字面量**（照拄才能
  逐位一致，写成 `8.0f` 会移动末位）。`IQ_MEASURED` 模式下 `iq_ref` 取 `iq_filter` 的较小幅值。
- 环内顺序照原版：**MTPA → FW → 作用于本周期的 id/iq 设定值**（`target_id/iq` 保持命令值，
  与原版区分「命令」与「本周期设定值」一致）；FW 的 q 轴修正用 `mod_q_filter`。
- 新增两个滤波状态：`duty_abs_filtered`（LP 0.01）与 `mod_q_filter`（LP 0.2），均按原版
  截断到模 ≤ 1。`SIGN` 合并为单一 `foc_sign`（原版 `util/utils_math.h:58`，`x==0` 为 +1）。
- **未携带（B3 剩余件，已定位）**：原版 FW 内还置 `m_current_off_delay = 1.0`，其唯一
  读者是**调制延长块**（`mcpwm_foc.c:3953-3990`，靠 `m_i_fw_set < min_current` 与
  `m_motor_released` 决定是否继续保持调制）。该块未移植，所以这里刻意**不设该字段** ——
  设了就是无人读的死状态（与本端口对 `set_current_off_delay` 的处理同一理由）。另，
  `m_i_fw_override` 那条路径来自检测流程，属 **B5**。

#### B4 已完成：饱和补偿与温度补偿

- **饱和补偿**（`foc_math.c:36-65` 的三个分支）与**凸极项**早已随观测器一起逐位对齐
  （`foc_observer_adjust_params`，见一致性表）。
- **温度补偿**这次接通了整条链：`timer_update`（mcpwm_foc.c:3939-3948）在控制周期开头**无条件**重算
  `comp_fact = 1.0 + 0.00386 * (motor_temp - foc_temp_comp_base_temp)`，把 `foc_motor_r` 与
  `foc_current_ki` 各乘一次得到 `m_res_temp_comp` / `m_current_ki_temp_comp`；**只有使用**由
  `foc_temp_comp` 门控，两处消费者即 `foc_math.c:70-72`（观测器的 R）与 `mcpwm_foc.c:4634-4637`
  （电流环的 ki，两轴共用）。`<= -30 °C` 是原版「NTC 没有可用读数」的情形，直接走未补偿参数。
- **表达式的形状也要保住**：参考里 `1.0` 与 `0.00386` 是 **double 字面量**，乘积在 double 里算完才
  窄化成 float。测试用一个**能判别**的温度（−40 °C、基准 25 °C）钉住这点：double 形状给
  `0x1.7f8a0ap-1`，写成 float 字面量会给 `0x1.7f8a08p-1`（差最后一位）。
- **这个输入从哪来**：原版在 **ADC 注入中断**里采样并滤波（FET 用 `UTILS_LP_FAST(..., 0.1)`，
  电机用板级 `MOTOR_TEMP_LPF`，默认 0.01；电机那路另有「读数不像温度就替换成 −100」的保护），
  FOC 只读缓存值。所以产品侧新增 `vesc_host_sample_temperatures()`，按同一形式 `v -= f * (v - x)`
  滤波后交给 `foc_core`；滤波常数是 `hwconf/hw.h` 的**板级**定义，因此它属于产品而不是 app。
  这也让原先只有测试调用者的 `foc_core_set_temperature` 第一次有了生产调用者。
- **顺带消掉的两处「无源字段」**：`GET_VALUES` 与 `GET_VALUES_SETUP` 的 `temp_motor`
  现在报真实滤波值（原先是命名 0），统计里的电机温度均值/最大值也从 −300 种子变成真实值。
  另外把 `fet_temp_c` 的种子从本端口自选的 **25 °C** 改回参考的 **0 °C**（参考的静态零初始化，
  由滤波从那里爬上去）——这是既存的小偏差，既然一行能消掉就不再登记。
- 测试：`test_foc_temp_comp_factor`（基准处**精确为 1.0**、系数、double 形状判别）、
  `test_foc_core_temp_compensation`（基准温度与 −30 地板下与未补偿**逐位相同**、地板边上仍补偿、
  热电机抬高 ki 使同一电流误差下电压更大、把 ki 置 0 后只剩观测器这一个消费者时输出仍不同、
  统计报出所给电机温度）、`test_vesc_host_temperature_sampler`（滤波收敛形式、坏读数替换、
  NULL 目标拒绝）。证据：55/55、两个二进制各 10 次无抖动、ASan/UBSan 绿、产品端到端仍跑通
  （RPM 217.7、0 错误）。
- **仍未做（登记在一致性表）**：`l_temp_motor_start/end` 与 FET 温度启用的**降流**（`utils_map` 那条）。
  它共用这个输入，但属于限制逻辑。

#### B5 检测流程：结构结论（先读再动，避免把阻塞过程硬搬）

`mcpwm_foc_measure_resistance()`（mcpwm_foc.c:1797）这类过程在参考里是**阻塞式**的：
它 `mc_interface_lock()`、置一套覆盖状态（`m_phase_override` / `CONTROL_MODE_CURRENT` /
`MC_STATE_RUNNING`）、修改 timeout、然后 `chThdSleepMilliseconds` 循环等待，靠 **ISR 每周期
加进 `m_samples.avg_current_tot/avg_voltage_tot/sample_num`** 来采样，最后除出平均值；
失败路径要 `stop_pwm_hw()` 并回滚状态与 timeout。

端口没有线程，所以**不能照搬阻塞形状** —— 应投影为**状态机**：一个\u201c爬流 → 稳定等待 →
计数采样 → 收尾/回滚\u201d的相位机，由快环或周期 poll 推进，采样累加器放在 foc_core 里（注意
它与遥测用的 `foc_core_read_reset_averages` 是**两套**累加器，原版就是分开的）。
`measure_inductance`（:1909）与 `measure_inductance_current`（:2086）同形。
实现时还要补：相位覆盖字段（现在只有 handbrake 的置 0）、`stop_pwm_hw` 等价物、以及
`COMM_DETECT_*` 族的受理（当前在命令表外）。

**B6 剩余项的实测依赖（读原版后记录，避免下轮重新推导）**

- `COMM_SET_CURRENT_REL`（**已实现**）：线格式是 float32 × 1e5（原版用的是**定点**
  惯例：存 int32 = 值× scale，读取时除回去，不是 IEEE 浮点），语义在
  `mc_interface_set_current_rel()`（mc_interface.c:733）：按 `duty` 的符号选限幅基数 ——
  `|duty| < 0.02` 或 `SIGN(val) == SIGN(duty)` 时用 `lo_current_max`，否则用
  `|lo_current_min|`（`SIGN(0)` 是 **+1.0**，所以零设定值配负 duty 会选负限）。因此它
  必须在电机侧而不能在编解码层，因为编解码层没有 duty。结果再走
  `mc_interface_set_current`（DIR_MULT 在 glue 施加）。
  末尾仍有一个由 `l_abs_current_max` / `cc_min_current` 门控的
  `set_current_off_delay(0.1)` 副作用待接（见下）。
- 上述三个字段的**默认值来源不同，别当成一类**：`cc_min_current` 在
  `mcconf_default.h` 有全局默认 `0.05`；而 `lo_current_min` 与 `l_abs_current_max`
  **没有全局默认**（`lo_*` 系列是按硬件标定的）。所以前者可以直接写进
  `motor_config_set_defaults`，后者必须由板级（`board/<board>`）提供 —— 若端口先按 0
  占位，必须写明“待板级提供”，不可当成原版默认值。
- handbrake（**已实现**）：原版链路是
  `COMM_SET_HANDBRAKE`（float32 × 1e3 —— 安培，不是相对命令的 1e5）→
  `mc_interface_set_handbrake()`（|current|>0.001 时 SHUTDOWN_RESET；`mc_interface_try_input()`
  为真则整体 return；按 motor_type 分派；最后 `events_add("set_handbrake", current)`）→ FOC 走
  `mcpwm_foc_set_handbrake()`：设 **`CONTROL_MODE_HANDBRAKE`**、把电流写进 **iq** 设定值、
  **不取绝对值**、**不乘 DIR_MULT**。模式本身的含义在环内：
  `mcpwm_foc.c:3602` 强制 `phase = 0`（“让电流简单地把转子锁住”，而不是出力矩）。端口已按
  此实现：新增 `FOC_STATE_HANDBRAKE`（追加在枚末尾，不重编号）、
  `foc_core_set_handbrake` 不再取绝对值/不再写 d 轴、快速环在进入该模式时把角度置 0
  （同时使上报相位与扇区测速跟随该角度），电流 PI 分支纳入该模式。
- **`duty_now` 不是相占空比**（已修）：原版 `mcpwm_foc.c:3818` 定义
  `duty_now = SIGN(vq) * NORM2_f(mod_d, mod_q) * p_duty_norm`（`mod = v * 1.5 / v_bus`，
  `p_duty_norm = TWO_BY_SQRT3 / foc_overmod_factor`，overmod 默认 1.0）。端口原先写的
  `duty_a`（A 相占空比）是完全无关的量，而它正是 `COMM_GET_VALUES` 的线上 duty 字段、
  按 duty 降流的输入、以及 `COMM_SET_CURRENT_REL` 的分支判据。
- **命令环不夹紧，且有方向乘子**（已修）：`mc_interface_set_current()` 只做
  `SHUTDOWN_RESET()`（|current|>0.001）、`mc_interface_try_input()` 门控、
  `mcpwm_foc_set_current(DIR_MULT * current)` 与 `events_add`，**不做限幅**（限幅在 app 层
  与弱磁）。端口原先在 `foc_core_set_current` 里夹紧到 `[current_min_a, current_max_a]`
  （原版没有）且未乘 `DIR_MULT`；今已去夹紧，`DIR_MULT` 按原版**逐命令**在 glue 施加
  （current / brake / duty / pid_speed 有，**handbrake 特意没有**）。
- **刹车仍缺控制模式（已定位到具体环内语义，待 B3）**：原版刹车是
  `CONTROL_MODE_CURRENT_BRAKE` + `m_iq_set = DIR_MULT * current`（**不取反**，
  mcpwm_foc.c:828）。模式本身的含义分布在环内四处，**不可用“负电流”近似替代**：
  `mcpwm_foc.c:3450` `iq_set_tmp = -SIGN(speed_fast_now) * fabsf(iq_set_tmp)`（刹车电流
  逆着转速）；`:3328` `utils_truncate_number_abs(&iq_set_tmp, -conf_now->lo_current_min)`
  （幅度上限）；`:3391` `current_max_for_duty = fabsf(lo_current_min)`（按 duty 的限流基数
  换成正限）；`:4091` 刹车模式下清零 min-rpm 迟滞。真正的大头是 `:3350` 的**主动刹车状态机**
  —— “把三相一直短接（duty=0）直到刹车电流达到设定或上限，再回到电流控制，且至少停留
  10 个周期”（用到 `m_br_speed_before` / `m_br_vq_before` / `m_br_no_duty_samples` /
  `m_duty_filtered`），并且 `foc_math.c:722` 说明弱磁在刹车模式下也参与。所以它与 B3 共用
  一套状态，端口目前的 `-current` 近似**保留不动**，不自行翻转符号。
- **有效限幅的来源（上一轮写错过，以本条为准）**：`l_current_max/min` 是**存储的**配置；
  命令实际用的是 `lo_current_max/min`，它们是 `update_override_limits()`
  （mc_interface.c:~2500-2546）**运行时算出**的有效限幅 —— 取 MOSFET/电机电流限、rpm 限、
  加减速限、FET/电机温度限、duty 限、输入电流限的**最小绝对值**，再用
  `±cc_min_current` 兜底。因此：
  - **「按 duty 降流」确实存在**，就在这里（`lo_max_duty`、`lo_max_i_in` 等参与取最小），
    不属于 B4；
  - `confgenerator_serialize_mcconf()` **不写**这两个字段，端口也不应把它们当配置持久化
    （schema 6 已改：只写 `l_abs_current_max` 与 `cc_min_current`）；
  - 端口 `foc_core_set_current_rel` 目前用 `current_min_a`（即 `l_current_min`）当负限幅基数，
    而原版用 `lo_current_min`（有效值）。只有在本端口算出有效限幅之后两者才等价。
- 原 B6 计划里的另一项已核实**归属错误**：`utils_step_towards(&m_iq_set, ...)` 的斜坡在
  `mcpwm_foc_measure_resistance()` 里（mcpwm_foc.c:1818），属于 **B5 检测**；
  `utils_map(fabsf(duty_now), 0, 40/v_bus, 0, foc_observer_gain)`（mcpwm_foc.c:4150）是
  **观测器增益随 duty/v_bus 缩放**，属于 **B4**（它是 `duty_now` 的消费者之一，duty_now 已修正）。

#### B5 进展：电阻测量已按原版迁移，并查出对 B2 的依赖

之前 `app/motor_id` 里的「检测」是**自造的**：电压从 0.1 V 起、每步 +0.05 V 边爬边累加，然后报
`R = (Σv/Σi) * 2/3`；电感用 `L = v·dt/di`，磁链与霍尔也是拍出来的。这些数字无法对照原版，按
「不编造测量值」的原则已全部删除。

**已迁移：`mcpwm_foc_measure_resistance`（mcpwm_foc.c:1797-1908）**，逐段对应：

| 原版 | 本端口 |
| --- | --- |
| `m_phase_override = true` + `m_phase_now_override = 0` | `foc_core_set_phase_override(0, true)`；`foc_core` 在取角前判它，置位时**不读传感器也不跑观测器**（原版 :3486/:3524/:3532 都在各自分支前判这个标志） |
| `m_control_mode = CURRENT`、`m_id_set = 0` | 端口 `set_current(iq)`（id 恒为 0） |
| `while (|iq_set-current|>0.001) { step(|current|/200); fault; sleep 1ms; }` | RAMP 相，每累计 1 ms 走一步；`utils_step_towards` 按原版逐字转写 |
| `chThdSleepMilliseconds(50)` | SETTLE 相，50 ms |
| 清 `m_samples` 后等 `sample_num >= samples`（上限 10000 ms） | SAMPLE 相；清与读是**两个**端口调用，因为原版就是在不同时刻用它们 |
| `m_samples += NORM2(id,iq)` / `NORM2(vd,vq)`，仅 `MC_STATE_RUNNING` 时累加 | `foc_core` 的检测累加器，以「控制环跑过的周期」为门（本端口 `state >= RUNNING_CURRENT`） |
| `R = voltage_avg / current_avg`（各自先 /n） | 同序计算：先两个平均再相除——直接算 `Σv/Σi` 代数相等但**不是逐位相同** |
| 两条 fault 路径 + 收尾块（清 id/iq、清 override、停 PWM） | `motor_id_fail()` / `motor_id_stop_motor()`，后者只由 `stop_after` 决定是否在结束时执行（原版给 R→L 两趟之间留电机在跑） |

**一处必须做对的地方（我先做错了）**：原版的 `while` 是「先测条件、再步进、再睡」，所以**达成目标的那一毫秒
就是斜坡的最后一毫秒**（不再多睡一次）。最初的状态机让「发现已达成」的那一毫秒空转，斜坡多花 1 ms；
测试用 199/200 ms 两个点把它钉住了。

**发现的结构性依赖**：`mcpwm_foc_measure_inductance` 一开头就把配置改成 **HFI 模式**
（`foc_sensor_mode = FOC_SENSOR_MODE_HFI`，:1927-1935）并驱动 `foc_hfi_voltage_*`。所以**电感测量、
以及包含它的 `COMM_DETECT_MOTOR_R_L`，都依赖 B2（HFI）**。磁链（`conf_general_measure_flux_linkage`，
开环升速看电流下降）与霍尔检测的程序**尚未读**。三个未迁移程序一律返回 `EDGE_ENOTSUP`，不再给近似值。

**证据**：`tests/test_app_motor_id.c` 用已知电阻的假机器验证各相/时序/斜坡速率/累加器只被清一次/
两条 fault 中止/采样超时（原版在超时路径上就是除以零，本端口照算但把结果标为 invalid）/`stop_after`
两种分支；`tests/test_product_glue.c` 的端到端测试让**原版程序驱动真实控制环**跑虚拟电机，量回它的
0.05 Ω。55/55、两二进制各 10 次无抖动、ASan/UBSan 绿、六门禁绿、产品端到端照跑。

#### B2 进展：HFI 的角度跟踪器已逐位对齐（并挖出一个既存保真缺陷）

B2 的面很大（mcpwm_foc.c 内 234 处 HFI 引用），但可以先把它拆开：**角度跟踪器**是纯函数、可逐位
验证；**激励与采样**（六矢量/V0V7 注入、按 PWM 周期取电流、翻转计数）住在 ISR 里，是下一刀。

- **已迁移**：`foc_hfi_adjust_angle`（foc_math.c:766-778）。比例项 + 双积分器把注入坐标系的角度拉向
  电流测量给出的误差，双积分器由**快速转速估计**限幅（`fabsf(m_speed_est_fast)`，这是它不会随转速发散
  的原因）；角度误差先按 `foc_hfi_max_err` 截断。原版的 `4000.0` / `10.0` 是 **double 字面量**，
  两个增益都是窄化后的乘积，按原样保留（原版自己也有个 TODO 说这俩的比例没复核过，因此照抄而不是“整理”）。
- **差分证据**：harness 直接编译参考的 `motor/foc_math.c`（桩头只需 `ch.h`/`hal.h`/`hw.h`，后者仅用到
  `HW_HAS_PHASE_SHUNTS` 一个宏），两边跑同一序列（3 组 `max_err` × 3 组 `gain` × 4 组转速限幅 × 400 步，
  状态跨组延续），**36 行输出逐位文本相同**。这 36 个向量即 `test_foc_core.c` 里的黄金向量。
- **一条方法论教训**：第一次比对得出 “ALL MATCH” 是**无效的** —— 两边用了不同编译旗标（图省事用了
  `cc` 默认），而且只比了第一行。用同一体制、比全量之后才看到漂移。**差分 harness 两侧必须同旗标，
  且必须比全量**。
- **挖出的既存缺陷（已修）**：`app/foc_core/include/foc_core/foc_math.h` 把 `M_PI` 定义成
  **带 `f` 后缀的 float**，而参考用的是 glibc 的 **double** `M_PI`。于是每次角度回绕
  （`*angle += 2.0 * M_PI`）都比参考差约一个 float ULP，单次不可见、几千步后累积到第 5 位
  （harness 量到的正是这个）。严格 `-std=c11` 不暴露 glibc 的 `M_PI`（参考树要 `_DEFAULT_SOURCE`），
  所以这个定义就是本端口实际用的值 —— 已改为 double 字面量。修正后端口与参考 36×400 步逐位一致。

- **同一刀下去的还有 HFI 的 FFT 抽头**（`util/utils_math.c:509-597` 的 9 个：8/16/32 点的 bin0/1/2，
  即均值、基波、二次谐波）—— HFI 的角度误差就是由它们算出来的。抽头逐字转写；它们索引的四张 32 项
  cos/sin 表是**六位小数的取整常数**（不是 `cosf/sinf` 的输出），因此**生成而非转写**：
  `tools/gen_hfi_from_reference.py` 从参考读出四张表、校验「每张 32 项且形如 `-?\d+.\d{6}`」后写出
  `app/foc_core/src/hfi_tables.h`，并支持 `--check`（供 CI 判「生成物是否过期」）。表放在 app 里，
  理由同 D30（app 不得依赖 infra，这也是 `vesc_can`/`motor_config` 各自带一份 crc16 的原因）。
- 差分证据：同一份驱动编译两次（一次接参考的 `utils_fft*`，一次接本端口的 `foc_fft*`），四种输入
  （冲激、斜坡、伪随机、干净基波）× 9 抽头 × 实/虚部**逐位文本相同**，这 4×18 个值即
  `test_foc_core.c` 里的黄金向量。

### 阶段 C — 配置与持久化

#### C3 定界（先读再动；更正上一轮自己的指向）

上一轮把 C3 指向 `flash_helper.c`，**那是错的**：`flash_helper.c`（534 行）与它的头只做
**固件镜像/引导**——`flash_helper_erase_new_app` / `write_new_app_data` / `code_data` /
`jump_to_bootloader`、STM32 扇区地址表（SECTOR_0..11，16K/64K/128K）——与配置持久化无关。

C3 的真正目标是 **`driver/eeprom.c`（643 行）**：在 flash 扇区上做 EEPROM 仿真，带**扇区轮换**
与备份，正好对应判据那三项——**擦除粒度**（整扇区）、**写前擦除**（先写另一扇区、校验后再标记）、
**掉电中断后的可恢复性**（旧扇区在切换完成前仍是完整的一份）。它上层的配置读写是
`conf_general.c`（2279 行）里的 store/load。

端口现状：`infra/flash` 是个 **host 侧假实现**（自述 "deliberately trivial host-side fake"，
按 key 直接 memcpy），只有 `flash_read/flash_write` 两个函数；`motor_config` 目前用自有信封
（signature + version + length + CRC，原版没有）落盘。

**C3 要复现的设计（读参考后确认）**：`driver/eeprom.c` 用的是经典的 **AN2594 两页仿真**——
`PAGE0_BASE_ADDRESS`/`PAGE1_BASE_ADDRESS` 由 `EEPROM_START_ADDRESS` 切出，运行期只认一个
`ValidPage`；每条记录是「虚拟地址 + 数据 + 校验」的头（所以写入不改动旧记录，只是追加），
当前页写满时把**有效记录搬到另一页**再标记新页有效；初始化/恢复靠**扫描两页**找出有效页
（这就是判据里「掉电中断后的可恢复性」：切换完成前旧页仍是完整的一份）。
所以 C3 的形状：把扇区轮换/追加写/页搬移/扫描恢复的**语义**移入 `infra/flash`（配一个
扇区后端端口，以便在 host 上用 mock 注入掉电），再接上 `motor_config` 的存取路径。

#### C3 已完成（判据三项都有测试）

`infra/flash` 现在有 `flash_emul.c`：AN2594 两页仿真，扇区后端走端口注入，而不是写绝对地址。

- **擦除粒度**：mock 只接受整扇区擦除（并断言长度），仿真也只在两处调用它——格式化与搬移时
  擦旧页。
- **写前擦除**：快路径只追加、从不擦；搬移先把新页写全（含按消费者变量表搬运的最新值），
  再擦旧页。
- **掉电可恢复**：搬移中途断电时旧页仍是完整有效的一份 → 重启 init 取旧页、读到上次提交值；
  测试还断言此时**没有**多出擦除——这正是它存活的原因。
- **如实钉住**参考自身的一处粗糙：它是“先擦旧页、后标 VALID”，两者之间断电会两页皆无效、
  下次 init 只能格式化（数据丢失）。测试显式覆盖这个窗口，而不是假装不存在。
- 测试也抓到端口里一个真 bug：`flash_emul_init` 原先用**写侧**判据找页（“我能往哪写”），
  于是“搬移中断、新页仅 RECEIVE”的半成品镜像会被当成可用、读出一片空；正确判据是**读侧**的
  “是否存在 VALID 页”（参考 `EE_Init` 的口径）。

#### C3 后续：配置持久化已改为变量式（含对本文先前一句错的更正）

先更正：本文早先写过「配置 blob 的持久化属 `conf_general.c` 那套 flash store，不是 EEPROM 仿真」
——**恰好相反**。读 `conf_general_read/store_mc_configuration`（conf_general.c:436-520）可见：
配置**就存在 EEPROM 仿真里**，用 `EE_ReadVariable/EE_WriteVariable`，虚拟地址基址
**`EEPROM_BASE_MCCONF = 1000`**（第二电机 5000，见 conf_general.c:50-54），
**每 2 字节配置占一个 u16 变量**（循环 `sizeof(mc_configuration) / 2` 次），且变量表就是在
`conf_general.c:72` 处用 `EEPROM_BASE_MCCONF + i` 枚举出来的——这正好就是 C3 里
`flash_var_table_t` 需要的形状。

完整性不是外挂的 CRC 信封，而是**结构体自带的 `uint16_t crc;` 成员**（生成结构体的最后一个
成员，之前在 manifest 里按“不进线上流”处理是对的：原版 `confgenerator_serialize_mcconf` 确实
不写它）；写入前用 `mc_interface_calc_crc(conf, is_motor_2)` 算好，读出时比对，**任何变量缺失
或 CRC 不符就 `confgenerator_set_defaults_mcconf()` 回落默认值并记故障**。

因此本端口要做的：把 `motor_config` 现有的**字节存取端口**换成**变量存取端口**
（`flash_emul` 正好是这个实现，由产品侧 glue 提供——app 不得直接依赖 infra），基址 1000、
表 = base+i、crc 成员参与校验、失败回落默认值——这就是自有信封（signature + version +
length + CRC）的替代物。实现前要读的最后一处——`mc_interface_calc_crc` 的字节范围——**已读**（见下）：算法本身是
util/crc.c 的 crc16，端口已在 `vesc_can` 里有一份位算式副本。

**已读到（最后一块）：CRC 的范围与它带来的一个陷阱**

`mc_interface_calc_crc`（motor/mc_interface.c:3067）的做法是：把 `conf->crc` 临时清零，
对**整个 `sizeof(mc_configuration)`** 算 `crc16`，再把 `crc` 原来的值恢复。也就是说
**结构体的填充字节也参与校验**（不是“到最后一个字段为止”）。含义有两条，都要写进实现：

- **填充字节必须是确定的**：端口生成的结构体有填充，堆栈上的临时对象其填充是垃圾，
  同一份配置会算出不同的 CRC。所以校验必须对**长期存活、且在 `motor_config_set_defaults`
  里被 `memset` 过**的那一份（聚合根里的 `self->mcconf`，或同构的 staging）做，不要对
  一次性的栈对象做。
- 因为含填充，这个 CRC 只在 **同一份结构体镜像（同一 ABI）** 内可复现——这正是原版的
  行为，不是缺陷，但也意味着它不能当跨版本校验用。
- 因此本端口又要一份 crc16 副本（app 不得依赖 infra，而仓库的 D30 决定不建共享 util），
  与 `vesc_can` 里那份同理，用测试交叉校验即可。

**已完成（本次）**：上述设计已落地，自有信封随之删除。

- `motor_config` 的 `save`/`load` 现在自己走变量循环（一个 u16 对两个配置字节），端口由
  `motor_config_construct` 注入；先前为「先加路径、后切端口」而存在的
  `motor_config_store_to_vars`/`load_from_vars` 是**同一行为、两个公开入口**，已折回
  `save`/`load`（`ponytail-review` 的发现）。
- 删除的东西：字节存取端口 `motor_config_storage_port_t`、自有信封
  （`MOTOR_CONFIG_SIGNATURE`/`MOTOR_CONFIG_SCHEMA_VER` + length + CRC 与 `calc_crc`）、
  以及 1024 字节的 `scratch`。`MOTOR_CONFIG_BUFFER_SIZE` 保留，但现在只是两个线上流
  所需缓冲区的大小。调用方存储契约随之从 3576 降到 **2552**（本模块自己的 `-std=c11`
  下实测；`-std=gnu11` 布局相同但小 256 字节，这个差在前几轮已记过）。
- 产品侧：`vesc_host_make_flash_sector_port` 取代原先的字节存储端口——按 `flash.h` 的端口
  契约实现（**整扇区擦除**、**半字写只允许清位**、越界拒绝而不夹断）；
  `glue_state.flash_mem` 从 1024 扩到**两个扇区**（`2 × FLASH_EMUL_PAGE_SIZE`，
  仿真本身要这么多）；组合根枚举 `VESC_HOST_MCCONF_BASE + i` 的 **388** 项变量表，
  `flash_emul_init` 之后把变量端口交给 `motor_config`。
- `main.c` 另有一处必要修正：`glue_state` 清零后 flash 是**全 0**，而全 0 会被仿真读成
  「两个页都 valid」（marker 就是 `0x0000`），所以要先 `memset(flash_mem, 0xFF, …)`
  当作擦除态。
- 一个值得记住的坑（测试逼出来的）：**全零镜像的 CRC 恰好是 0**，与未写入结构体自带的
  `crc = 0` 相等，于是「空的存储」会被当成合法配置载入。原版能区分是因为**读失败**
  （`EE_ReadVariable` 对未写过的变量报错）——所以「空」必须由**读失败**判定，而不是由值
  判定。测试的 mock 现在也这样建模（写入才让变量存在）。
- 证据：55/55 通过、`test_motor_config` 与 `test_product_glue` 各连跑 10 次无抖动、
  ASan/UBSan 下相关四个测试全绿、六个门禁脚本全绿，且**产品本体端到端跑通**
  （RPM 217.7、0 错误）——这条路径在真实运行里成立，而不只是测试里。

#### C1 验收（有消费者的字段是否都配置驱动）

机械核对的结果（脚本对比 `foc_config_t` 的字段与 main.c 的 `mc->` 映射）：

- `foc_config_t` 共 **32** 个字段，**31** 个直接来自 `mc->某字段`，唯一的例外是
  `sensorless_mode` —— 它曾经被**硬编码为 false**，等于让配置里的传感器模式对控制路径不可见。
  原版是从 `foc_sensor_mode` 选角度源的（`mcpwm_foc.c` 的 `FOC_SENSOR_MODE_SENSORLESS` 分支），
  现已改为 `(mc->foc_sensor_mode == FOC_SENSOR_MODE_SENSORLESS)`，即 32/32 配置驱动。
- 结构体本身是参考的：**177 个成员**由生成器从 `datatypes.h` 产出（C2）；默认值是 **202 个宏**，
  每个都带它在 `mcconf_default.h` / `appconf_default.h` 的**行号**（判据要求的逐条对照）。
- 其余 **156** 个 mc 字段目前未被 `foc_core`/main 引用 —— 它们的消费者各有归属：HFI 系 →**B2**、
  温度补偿 →**B4**（受阻于无电机温度源）、检测系 →**B5**、编码器/DRV8301/NTC/BMS/Wi-Fi/埴位表
  等 →各自模块与 C3/D 阶段。所以本条判据「只补有消费者的字段」成立：**凡现已有消费者的，
  都已在配置里驱动**。

#### C2 进展：mc_configuration 流已逐字节对齐原版

`tools/gen_mcconf_from_reference.py` 从参考树机械生成头文件，与结构体合起来构成单一事实源：

| 生成物 | 内容 |
| --- | --- |
| `mcconf_enums.h` | 成员用到的枚举体 + `bms_config` |
| `mcconf_struct.h` | `mc_configuration_t`（177 个成员，按 `datatypes.h` 顺序） |
| `mcconf_manifest.h` | `MCCONF_WIRE(X)`：207 行线上项（含数组元素与 `bms.*` 子字段，按原版**序列化顺序**）、`MCCONF_SIGNATURE`、每种 kind 的长度 |
| `mcconf_defaults.h` | 202 个 `MCCONF_*` 宏，每个注明它在 `mcconf_default.h` 的行号 |

写、读、默认值都展开同一份 `MCCONF_WIRE(X)`，所以不可能互相矛盾；`MCCONF_WIRE_LEN` 静态断言为
原版的 **488**，测试用「默认配置 → 编解码」与**原版序列化器的输出逐字节比对**
（向量在 `docs/bldc-mcconf-format.md`）。生成器遇到无法解析的序列化语句就拒绝输出，
因此原版一变就会在这里炸——它已抓到我两个错：观测器默认值是 **3**（MXLEMMING_LAMBDA_COMP）
而不是 0；`l_abs_current_max` 其实有全局默认 **130 A**（宏名是 `MCCONF_L_MAX_ABS_CURRENT`，
我先前搜错名字才误记为“待板级”）。

仍待办：无。`app_configuration` 流已是原版的 290 字节布局（C2 的另一半）；flash 信封
（signature + version + length + CRC，原版没有）**已删**，持久化改走变量存储（见上）。

> **A7 与 C1 必须先合并做，且 C1 的字段集决定 A7 的字节流。** 原版
> `confgenerator.c` 是**唯一**的配置字节流：`COMM_GET_MCCONF` 回的就是它，
> 落 flash 存的也是它。本仓库当时的 `app/motor_config/src/serialization.c` 曾是
> **另一套自造格式**（自带 `MOTOR_CONFIG_SIGNATURE` + `MOTOR_CONFIG_SCHEMA_VER`
> + payload_len + CRC16 框）——**现已删**：协议用原版的 mc/app 两条流，
> 持久化用结构体自带的 `crc` 成员，两件事都回到原版的形状。

| 切片 | 内容 |
|---|---|
| C1 | 字段级 1:1：`si_motor_poles`/`si_gear_ratio`/`si_wheel_diameter`、`throttle_exp*`、`foc_current_filter_const`、`foc_dt_us`/`foc_f_zv`、`foc_motor_ld_lq_diff`、`foc_temp_comp*`、`foc_observer_*`、`foc_hfi_*`、`l_*` 等（按阶段 B 的消费者逐个补齐，不做无消费者的字段） |
| C2 | 按 `confgenerator.c` 的字段顺序/缩放写序列化器，**flash 与协议共用同一份字节流**；含跨版本迁移语义 |
| C3 | `infra/flash` 的 EEPROM 仿真（`driver/eeprom.c` 的两页仿真）+ 配置的变量式持久化 |

**A4c 的更正（先前的判断是错的）：** 曾据此断言"原版按霍尔/编码器步进计数、需要改转子端口契约"。
读完 `mcpwm_foc.c:3866-3881` 后否证：原版把 FOC **已有的相位**量化成六个 60° 扇区
（注释 "resolution = 60 deg as for BLDC"），对扇区序号做差分并做回绕修正，**不需要任何传感器**，
因此不需要改端口。教训记此：部分阅读得出的依赖不能当结论。

**C1 的第一个发现：极对数有两份真相。** 原版没有独立的极对数字段，FOC、虚拟电机、
速度换算全部取自 `si_motor_poles / 2`（`virtual_motor.c:126`、`mc_interface.c:1626`）。
本仓库 `foc_config_t.pole_pairs` 是第二份真相，没有任何机制保证两者一致 —— C1 应当
把它收敛成 `si_motor_poles`。

### 阶段 D — 组合根与真实硬件

| 切片 | 内容 |
|---|---|
| D1 | `product/vesc6_stm32f4`（ticket #203 声称已交付，实际不存在） |
| D2 | `soc/stm32f4`：TIM1/TIM8 互补 PWM、三路 ADC 注入采样、IRQ 转发（现为 31 行纯算术） |
| D3 | board 处置：`board/vesc4`/`board/vesc_unity` 已登记为「保留但不绑定」，`board/vesc6` 登记为「保留并指向 D1」（带触发条件）——均在下方处置表中 |
| D4 | 实时预算：原版 15µs @168MHz 的快环节拍；目前仓库内无任何基准工程 |

### 阶段 E — 收口

| 切片 | 内容 |
|---|---|
| E1 | 覆盖率回到门槛（当前 76%，main 94%，CI 门槛 95%） |
| E2 | `vesc_host` 进 CI `product-matrix` 运行列表 | 已完成 ✓（产品自检：故障或未起转即非零退出，CI 只判退出码） |
| E3 | 在 `adr-conformance.md` 登记 `bldc` family 与全部已知偏差 | 已完成 ✓ |
| E4 | 未接入但存在的模块明确处置 | 已完成 ✓（结论见下表） |

### E4 的处置结论（三选一，不留空白）

| 对象 | 结论 | 理由 |
|---|---|---|
| `board/vesc4`、`board/vesc_unity` | **保留但不绑定产品** | 两者在 `EDGE_LEGAL_FAMILY_BOARD` 里是合法组合、可交叉编译；绑定留给真正的 vesc4/unity 产品。当前无产品使用是**有意保留**，不是遗忘 |
| `soc/stm32f4` | **登记偏差** | 目前是纯算术适配；寄存器级驱动归 D2 |
| `board/vesc6` | **保留并指向 D1** | 它是 `product/vesc6_stm32f4` 的预期 board（两者同在 `EDGE_LEGAL_FAMILY_BOARD` 里），在 D1 落地前保持可交叉编译但不被任何产品绑定；**触发条件**：若 D1 被放弃，vesc6 与 vesc4/vesc_unity 同一处置（保留为端口参考）。三块 board 目前都**没有被 CI 逐个构建**——它们只是合法组合，矩阵枚举是 D39 的🟡项，所以 D3 走的是「登记处置决定」这条分支，不是「被产品绑定且被 CI 构建」 |
| 未实现的 `COMM_*` | **登记偏差 + 指明归属阶段** | 160 个 id 已全表声明；实际处理集见 `vesc_comm_process_command`。缺口归 A7（配置流）与 A8（转发/终端/其余） |
| `product/vesc6_stm32f4` | **实现** | ticket #203 声称已交付但不存在，归 D1 |

## 4. 工作方法（每次提交都这样）

1. **先读原版**，把该单元的语义（状态、不变量、副作用、顺序、边界）写清楚，再写代码。
2. 能编原版就编原版：差分测试是唯一能把「一比一」变成可证伪的东西。
   方法见 skill `differential-testing-a-port`。
3. **提交前跑 ponytail-review**：只看过度设计，输出「删什么、换成什么、省几行」。
4. 新逻辑留一个可运行的检查；数值型断言用原版**导出**的黄金向量，不用自己推导的值。
5. 提交信息只写做过的事；没做的不写。
6. **已知工具误报不修**：静态检查器会把 `__cplusplus`（以及 `_Static_assert`）报为「使用保留命名空间」。
   两者都不是用户声明的标识符：`__cplusplus` 是**编译器自己 `#define` 的**（`cc -dM -E -x c++ -`
   即可看到），而规则禁止的是*用户*声明 `__` 打头的名字；仓库有 **56** 个头文件、参考树有 **250** 个
   用同一 `#ifdef __cplusplus` + `extern "C"` 惯用法，这些都是本移植之前就有的行。按“改名”去做就是删掉
   C++ 链接守卫，会让 C++ 调用方按修饰名链接而断链——**属真回归，因此不改**。

## 5. 已知语义偏差（登记去向）

偏差清单**不在这里**。`adr.md` 是唯一决策源，`adr-conformance.md` 是唯一差异视图；
本文件只负责计划与判据。BLDC 族的偏差已登记在
[`adr-conformance.md`](adr-conformance.md) 的「BLDC / VESC port」一节。

已被修掉、不再属于偏差的前提（留作记录）：

- 极对数曾有两份真相（`foc_config_t.pole_pairs` 与 `si_motor_poles`）—— 已收敛为 `si_motor_poles`。
- 速度统计曾返回 0（缺 `si_*` 字段）—— 已打通。
- `tachometer` 曾被认为需要霍尔/编码器步进源 —— 读原版后否证，见阶段 A 的更正。
