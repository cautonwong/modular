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

| 切片 | 内容 | 状态 |
|---|---|---|
| A1 | packet 层（CRC16、8/16 位帧、解码指针语义） | 已验证 ✓ |
| A2 | 命令全表 160 个 id + `COMM_FW_VERSION` 字节布局 + identity 由组合根提供 | 已完成 ✓ |
| A3 | `GET_VALUES` / `SELECTIVE` 的**读清语义**与掩码 | 已完成 ✓ |
| A4 | 输入电流估计 + 电量累加器（打通 bit 3、9-12） | 已完成 ✓ |
| A4b | 电流低通链（`foc_current_filter_const` → `id_filter`/`iq_filter` → `i_abs_filter`） | 已完成 ✓（A4 的前置，读原版才发现的依赖） |
| A4c | `tachometer`（bit 13/14）：原版计的是**霍尔/编码器步进差分**，不是角度；需要转子端口暴露步进源 | 待办（阻塞于端口形状） |
| A5 | `GET_STATS` / `RESET_STATS` | 待办 |
| A6 | `GET_DECODED_ADC` / `GET_DECODED_PPM`（需要 `adc_input`、`ppm` 端口） | 待办 |
| A7 | `GET_MCCONF`/`GET_APPCONF` + `SET` 版（需要配置序列化器，见阶段 C） | 待办 |
| A8 | `COMM_FORWARD_CAN`、`COMM_TERMINAL_CMD`，其余按 VESC Tool 实际调用序列补齐 | 待办 |

### 阶段 B — 控制面补全

| 切片 | 内容 |
|---|---|
| B1 | 观测器族：MXLEMMING / MXV / ORTEGA_LAMBDA_COMP（现只有 ORTEGA_ORIGINAL） |
| B2 | HFI 无感启动（原版 `m_hfi` 有一整套状态机） |
| B3 | 弱磁（`foc_run_fw`）、MTPA 接回控制环（原版含 iq 重投影） |
| B4 | 电感饱和/凸极补偿、温度补偿（`foc_temp_comp`） |
| B5 | 检测流程：`COMM_DETECT_MOTOR_{PARAM,R_L,FLUX_LINKAGE}` 与 `foc_detect_*` |
| B6 | 控制模式语义：`l_current_max` 斜坡、按 duty 降流、`COMM_SET_CURRENT_REL` |

### 阶段 C — 配置与持久化

| 切片 | 内容 |
|---|---|
| C1 | `mc_configuration` / `app_configuration` 字段级 1:1（含 `throttle_exp*` 等目前缺失字段） |
| C2 | 配置序列化：对应原版 `confgenerator.c`，**含跨版本迁移语义**（旧配置升级不是可选项） |
| C3 | `infra/flash` 的扇区/擦写语义（`flash_helper`） |

### 阶段 D — 组合根与真实硬件

| 切片 | 内容 |
|---|---|
| D1 | `product/vesc6_stm32f4`（ticket #203 声称已交付，实际不存在） |
| D2 | `soc/stm32f4`：TIM1/TIM8 互补 PWM、三路 ADC 注入采样、IRQ 转发（现为 31 行纯算术） |
| D3 | `board/vesc6` 绑定到真实产品（现在 `EDGE_LEGAL_FAMILY_BOARD` 允许 `bldc:vesc6`，但**没有任何产品用**） |
| D4 | 实时预算：原版 15µs @168MHz 的快环节拍；目前仓库内无任何基准工程 |

### 阶段 E — 收口

| 切片 | 内容 |
|---|---|
| E1 | 覆盖率回到门槛（当前 76%，main 94%，CI 门槛 95%） |
| E2 | `vesc_host` 进 CI `product-matrix` 运行列表（现在只跑 meter 系产品） |
| E3 | 在 `adr.md` / `adr-conformance.md` 登记 `bldc` family 与三处已知语义偏差 |
| E4 | 未接入但存在的模块（`board/vesc4`、`board/vesc_unity`）明确处置 |

## 4. 工作方法（每次提交都这样）

1. **先读原版**，把该单元的语义（状态、不变量、副作用、顺序、边界）写清楚，再写代码。
2. 能编原版就编原版：差分测试是唯一能把「一比一」变成可证伪的东西。
   方法见 skill `differential-testing-a-port`。
3. **提交前跑 ponytail-review**：只看过度设计，输出「删什么、换成什么、省几行」。
4. 新逻辑留一个可运行的检查；数值型断言用原版**导出**的黄金向量，不用自己推导的值。
5. 提交信息只写做过的事；没做的不写。

## 5. 已知语义偏差（登记去向）

本文不列偏差。下面三类偏差必须写进 [`adr-conformance.md`](adr-conformance.md)，
在该文档登记之前，它们属于**未收口**状态：

1. 均值累加器的采样时机：原版采样器与控制环解耦且停机时仍累加（复用上次 vd/vq），
   本仓库在 `foc_core` 的周期 poll 中累加、停机时不累加 vd/vq。
1b. 电量累加器（amp/watt hours）的累加节拍：原版在周期性 MC 定时器 ISR 里用该定时器的
   `dt` 累加（`mc_interface.c:2036`），本仓库在快环里用环路 `dt` 累加。被积量同为
   ∫i dt，但采样率不同（此处更密）。
2. 无数据源的字段（电机 NTC、输入电流、三路 MOS 温度）在协议层返回 0，
   原版返回真实测量值。
3. `soc/stm32f4` 与 `board/vesc6` 目前是纯算术适配，不含寄存器级驱动。
