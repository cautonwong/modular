# Phase 1 — 打通经脉（已采纳）

> 对齐 [`adr.md`](adr.md)（D1-D85）与 [`.proposal.md`](../.proposal.md)。
> 追踪票：GitHub milestone **Phase 1 — 打通经脉**，tracking issue **#46**。

阶段 1 的目标不是“拼数量”，而是用**对立样本**验证分层架构与契约的弹性：跨指令集、跨宿主、跨领域各打通一条真实链路。

## 范围矩阵

| 维度 | 阶段 1 选择 | 验证形态 |
|---|---|---|
| CPU 架构 | ARM Cortex-M4（MPS2-AN386）、RISC-V 32IMC（`sifive_e`）、host（x86_64/ARM64） | QEMU / 本地 |
| 运行宿主 | bare-metal superloop、FreeRTOS 单任务“确定性执行舱” | QEMU / host |
| 外设端口 | UART、Storage(KV)、GPIO、Clock（框架只定契约，不写厂商寄存器驱动） | QEMU / host 假实现 |
| 产品领域 | `app/dlt645`（电表）、`app/modbus_slave`（工业现场总线） | 双协议共存 |
| 产品 | `meter_mps2_baremetal`、`meter_mps2_freertos`、`gateway_riscv_baremetal`、`meter_gateway_host` | QEMU + host |

## 执行修订（相对原始提案）

1. **分轴推进，不并行铺开**：RISC-V 裸机（复用 dlt645）→ FreeRTOS 宿主（ARM，复用 dlt645）→ `modbus_slave` → `meter_core` + 双协议 host。每步 CI 全绿再下一轴。
2. **PAL 分层不重复**：`edge/pal.h` 保留架构原语（critical/barrier/now/in_isr/isr_enter/exit）；`pal/os` 只加 `sleep`/`yield` 与 task-entry runner，不定义第二套临界区。
3. **端口命名对齐既有契约**：扩展 `edge/ports.h`（`edge_uart_port_t`、`edge_gpio_port_t`），沿用 `edge_storage_kv_t`/`edge_clock_port_t`，不另造平行类型；端口仍是可选规范形状（D14）。
4. **实体板属 HIL**：Black Pill / ESP32-C3 只作为 HIL 载体（#30），CI 门禁以 QEMU 为准。
5. **核心纯净守卫再收紧**：禁止 `lwIP`/`MQTT`/`LVGL` 等进入 `edge_module/sys/board/infra`；只允许作为与 modular 并列的宿主任务。
6. **`meter_core` 为可选出口**：双协议零预算违规可用 `dlt645` + `modbus_slave` 证明，`meter_core` 不阻塞竣工。

## 负向清单（Anti-Goals）

- 不手写芯片外设驱动库：一律 product glue 包装厂商 SDK。
- 不引入多线程数据竞争：内部永远单 runner 无锁 superloop，即使在 FreeRTOS 中也只占一个 Task。
- 不把重量级网络栈/图形库放进核心。
- 不放宽现有质量门：行覆盖率 ≥ 95%、无动态分配、map 分层预算、反例自测全绿。

## Definition of Done

- [ ] ARM MPS2 与 RISC-V `sifive_e` 在 CI 全自动编译 + QEMU 冒烟绿灯
- [ ] FreeRTOS 单任务宿主压力：连续 ≥100k 事件/中断调度，无死锁、无泄漏
- [ ] 双协议共存产品（单 runner）执行预算违规 = 0，N1–N5 守卫通过
- [ ] 行覆盖率 ≥ 95%，map 分层预算不超标，反例自测全绿

## 依赖与追踪

- #43 `pal/os` + RTOS runner；#44 RISC-V QEMU；#32 中立性验收
- #14 端口命名对齐；#15 全量组合矩阵；#30 HIL（非门禁）
- 工具链可行性（已验证）：`gcc-riscv64-unknown-elf` 支持 `-march=rv32imc_zicsr -mabi=ilp32`；`qemu-system-misc` 提供 `qemu-system-riscv32 -M virt`。

## 进度

- [x] **#44 RISC-V 32 QEMU 目标**：`cmake/toolchains/riscv-elf.cmake` + `board/riscv_virt`（CLINT 机器定时器 + QEMU test finisher）+ `product/riscv_meter`，CI 在 `qemu-system-riscv32 -M virt` 跑通“定时器 IRQ → 事件 → superloop → 退出”，复用同一 `app/dlt645`、app 零改动。
- [x] **#43 `pal/os` + FreeRTOS 单任务宿主**：`pal/os`（yield/sleep）+ `edge_os_idle_hook`；`pal/rtos/freertos` 提供中性 `edge_rtos_*` 宿主契约（RTOS 头不出 `pal/rtos`）；`product/meter_mps2_freertos` 把同一 superloop 作为单个 FreeRTOS Task 跑在 QEMU Cortex-M4，兄弟 Task 经同一 sink 注入事件；CI 新增 `freertos-qemu` job。
- [ ] #47 `app/modbus_slave`
- [ ] #48 UART/GPIO 端口契约
