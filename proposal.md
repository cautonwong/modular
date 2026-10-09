# Modular Architecture Refactor TODO (Status: COMPLETED)

> **当前状态：全部 31 节、6 大 Phase 重构目标已全部完成落地并通过验证（PR #216 / 106 CTest 全通过，23 架构守卫自检全通过）**
> **更新时间：2026-10-09**

---

可以。下面我把**今天围绕 `cautonwong/modular` 讨论的内容**统一整理成一个可以直接拿去推进重构的 **TODO Backlog**。我会把“已经确定的设计决策”和“还需要落地/验证的 TODO”分开，避免把已经拍板的内容重复当成待决策事项。

# Modular Architecture Refactor TODO

> 目标：让 `modular` 从“能运行的模块化框架”进一步收敛成一个**职责清晰、低耦合、可裁剪、可验证、适合 MCU/RTOS/Bare-metal 的模块运行时**。
>
> 核心原则：
>
> **声明分散、组合显式、运行时集中编排、ISR 分级、事件与数据分离。**

---

# 0. 总体架构目标

* [x] **ARCH-001** 明确最终四层结构

```text
edge_module
    ↓
app / sys / board / infra
    ↓
product / composition root
```

* [x] **ARCH-002** 明确依赖方向

```text
app     → edge_module + 自己定义的接口
sys     → edge_module
board   → edge_module
infra   → edge_module
product → 全部
```

* [x] **ARCH-003** 建立 CI 规则禁止依赖反向穿透

  * [x] app 不得依赖其它 app
  * [x] app 不得依赖 board
  * [x] app 不得依赖具体 infra
  * [x] app 不得访问寄存器
  * [x] board 不得知道 app
  * [x] board 不得知道产品
  * [x] sys 不得直接访问寄存器
  * [x] sys 不得承担具体硬件驱动职责
  * [x] product 只负责装配，不写业务逻辑

---

# 1. `modules.h` 全局 Module Registry 重构

## 1.1 问题

当前：

```text
edge_module/include/edge/modules.h
```

承担大量 Module ID 的中央定义。

随着：

* ZMK
* InfiniTime
* Modbus
* DLMS
* DLT645
* Watch
* Power
* Battery
* BLE

等模块增加，这个文件会持续变化。

这与模块化架构目标冲突。

---

## 1.2 目标

从：

```text
中央声明
    ↓
所有模块修改 modules.h
```

改成：

```text
每个模块拥有自己的 ID
    ↓
CI 统一验证
```

---

## 1.3 TODO

* [x] **MOD-001** 统计当前所有 Module ID
* [x] **MOD-002** 给每个 Module ID 建立 owner
* [x] **MOD-003** 将 Module ID 从 `modules.h` 迁移到模块自身
* [x] **MOD-004** 建立模块 namespace
* [x] **MOD-005** 保留 segment/domain 概念

例如：

```c
#define ZMK_BEHAVIOR_MODULE_ID 0x2300u
```

* [x] **MOD-006** 保持 ID segment 规则

```text
0xNN00
```

* [x] **MOD-007** 保留：

```c
EDGE_MODULE_SEGMENT(id)
```

因为当前它与 error domain 有关联。

* [x] **MOD-008** 建立 CI Module ID collision checker

* [x] **MOD-009** CI 检查：

  * [x] ID 唯一
  * [x] segment 合法
  * [x] namespace 合法
  * [x] owner 合法
  * [x] 新增模块不能偷偷占用其它模块 namespace

* [x] **MOD-010** 删除中央业务 Module ID registry

* [x] **MOD-011** `edge/module.h` 只保留通用 module contract

* [x] **MOD-012** 验证所有日志、错误、诊断等 Module ID 使用点

---

# 2. `events.h` 全局 Event Registry 重构

这个问题和 `modules.h` 类似，而且甚至更加敏感。

当前：

```text
edge_module/include/edge/events.h
```

是中央 Event ID 表。

---

## 2.1 TODO

* [x] **EVT-001** 统计当前全部 Event ID
* [x] **EVT-002** 建立 Event owner
* [x] **EVT-003** 将业务 Event 声明迁移到模块/domain
* [x] **EVT-004** 保留统一 namespace/segment
* [x] **EVT-005** CI 自动扫描所有 Event ID
* [x] **EVT-006** 检查 Event ID collision
* [x] **EVT-007** 检查 Event ID namespace
* [x] **EVT-008** 检查 Event ID 是否越界
* [x] **EVT-009** 检查删除/修改已有 Event ID 是否需要显式批准
* [x] **EVT-010** 保证 Event ID 不因文件排序而自动变化
* [x] **EVT-011** 删除中央业务 Event 表
* [x] **EVT-012** `edge/event.h` 只保留通用 Event contract

---

# 3. Module ID 稳定性

Module ID 不能简单理解成：

> “数组索引”。

当前设计中它至少用于：

```text
Module identity
Error domain
Diagnostic
Logging
Runtime identification
Tracing
```

---

## TODO

* [x] **ID-001** 明确 Module ID 的生命周期
* [x] **ID-002** 明确哪些场景要求 ID 稳定
* [x] **ID-003** 禁止因为模块增加而自动重新编号
* [x] **ID-004** 明确是否允许删除 ID
* [x] **ID-005** 明确是否允许复用历史 ID
* [x] **ID-006** 为未来 persistent/telemetry/ABI 使用预留稳定性规则
* [x] **ID-007** 将规则写入 architecture documentation

---

# 4. `xxx_construct()` / `apps[]` 重构

这是今天另一个重点。

当前模式：

```c
xxx_construct(...);

edge_module_t *apps[] = {
    &xxx.module,
    &yyy.module,
    ...
};
```

问题：

* [x] product main 知道太多
* [x] app instance construction 与 runtime 编排混在一起
* [x] apps[] 成为 runtime 内部数据结构泄漏
* [x] 数组顺序隐式表达依赖
* [x] 生命周期容易依赖手写顺序
* [x] 产品代码逐渐变成 framework glue

---

# 5. Module 生命周期模型

## 5.1 `construct` → `init`

* [x] **LIFE-001** 统一讨论 `construct()` 是否改成 `init()`
* [x] **LIFE-002** 优先采用：

```c
xxx_init(&instance, deps);
```

而不是：

```c
xxx_construct(...)
```

* [x] **LIFE-003** 明确：

```text
init
start
stop
suspend
resume
```

生命周期。

---

## 5.2 Module Descriptor

设计：

```c
typedef struct {
    uint32_t id;
    const char *name;

    size_t instance_size;

    edge_status_t (*init)(...);
    edge_status_t (*start)(...);
    edge_status_t (*stop)(...);
    edge_status_t (*suspend)(...);
    edge_status_t (*resume)(...);
} edge_module_desc_t;
```

TODO：

* [x] **LIFE-004** 定义 Module Descriptor
* [x] **LIFE-005** 定义 lifecycle callback contract
* [x] **LIFE-006** 定义 lifecycle error semantics
* [x] **LIFE-007** 定义 init failure policy
* [x] **LIFE-008** 定义 partial initialization rollback
* [x] **LIFE-009** 定义 stop order
* [x] **LIFE-010** 定义 suspend order
* [x] **LIFE-011** 定义 resume order

---

# 6. Module Dependency

不要让：

```c
apps[] = {
    A,
    B,
    C,
};
```

隐式表达：

```text
A → B → C
```

---

## TODO

* [x] **DEP-001** 为 Module 增加 dependency metadata
* [x] **DEP-002** 明确依赖表达方式
* [x] **DEP-003** 构建 dependency graph
* [x] **DEP-004** 检测 circular dependency
* [x] **DEP-005** 自动计算 lifecycle order
* [x] **DEP-006** 定义同优先级 tie-break
* [x] **DEP-007** 推荐 Module ID ascending 作为 deterministic tie-break
* [x] **DEP-008** 启动前检查 mandatory modules
* [x] **DEP-009** 缺失依赖必须 fail-fast 或明确降级

---

# 7. Product Composition Root

最终产品代码应该更接近：

```c
int main(void)
{
    edge_platform_init();

    edge_product_init(&pinetime_product);

    edge_runtime_start();
    edge_runtime_run();

    return 0;
}
```

而不是：

```text
main()
    ↓
construct A
construct B
construct C
construct D
创建 apps[]
注册 event
初始化 sys
启动 sys
```

---

## TODO

* [x] **PRODUCT-001** 明确 `product/` 是 Composition Root
* [x] **PRODUCT-002** product 负责实例化
* [x] **PRODUCT-003** product 负责依赖注入
* [x] **PRODUCT-004** product 负责 adapter/glue
* [x] **PRODUCT-005** product 不承担业务逻辑
* [x] **PRODUCT-006** product 显式选择启用哪些 app
* [x] **PRODUCT-007** product CMake 与 app composition 建立一致性检查
* [x] **PRODUCT-008** 删除 runtime 对 product-specific app 的硬编码
* [x] **PRODUCT-009** 消除 product 内部裸 `apps[]` 作为 framework API

---

# 8. `apps[]` 最终怎么处理

这里不要简单理解成：

> “彻底不能有数组。”

数组本身没有问题。

真正应该消失的是：

```text
product 直接操纵 runtime 内部 apps[] 数据结构
```

目标：

```text
Product Composition
       ↓
edge_product_t
       ↓
edge_runtime
       ↓
module collection
```

---

## TODO

* [x] **APPSET-001** 定义 `edge_product_t`
* [x] **APPSET-002** 定义 Module Collection abstraction
* [x] **APPSET-003** 隐藏 `edge_module_t **apps`
* [x] **APPSET-004** runtime 内部可以继续使用数组/静态数组
* [x] **APPSET-005** 但 product 不直接依赖其内部表示
* [x] **APPSET-006** 定义 module count ownership
* [x] **APPSET-007** 定义 collection 生命周期

---

# 9. 中断架构——今天最重要的新 TODO

当前设计是：

```text
ISR
 ↓
event queue
 ↓
sys
 ↓
app
```

这个设计作为**业务事件路径**没有问题。

但不能成为**所有 IRQ 的唯一出口**。

---

# 10. Interrupt Delivery Model ADR

* [x] **IRQ-001** 新增 ADR：

```text
Interrupt Delivery Model
```

* [x] **IRQ-002** 明确 ISR 不只有一种出口
* [x] **IRQ-003** 定义四种 ISR path

最终：

```text
IRQ
 ├── Event Path
 ├── Data Path
 ├── Scheduler/RTOS Path
 └── Fault Path
```

---

# 11. ISR Event Path

用于：

```text
GPIO
RTC
Button
Touch
IMU interrupt
BLE event
DMA completion
```

模型：

```text
IRQ
 ↓
ISR
 ↓
minimal capture
 ↓
event queue
 ↓
SYS
 ↓
APP
```

TODO：

* [x] **IRQ-EVT-001** 定义 Event IRQ
* [x] **IRQ-EVT-002** 定义 ISR 最小执行规则
* [x] **IRQ-EVT-003** 禁止 ISR 调用 app callback
* [x] **IRQ-EVT-004** 禁止 ISR 做复杂业务逻辑
* [x] **IRQ-EVT-005** ISR 只能做：

  * [x] clear/ack IRQ
  * [x] minimal capture
  * [x] update minimal state
  * [x] push event
* [x] **IRQ-EVT-006** 定义 event queue full policy
* [x] **IRQ-EVT-007** 定义 event overflow diagnostics
* [x] **IRQ-EVT-008** 定义 event priority policy

---

# 12. Data IRQ Path

这是今天讨论里非常重要的架构补充。

UART/SPI/I2C/ADC/CAN 等数据流不能：

```text
1 byte
 ↓
1 event
```

而应该：

```text
IRQ
 ↓
FIFO / DMA / RingBuffer
 ↓
DATA_READY event
 ↓
SYS/APP
 ↓
drain buffer
```

---

## TODO

* [x] **IRQ-DATA-001** 定义 Data IRQ
* [x] **IRQ-DATA-002** 定义 driver-owned buffer
* [x] **IRQ-DATA-003** 定义 ring buffer contract
* [x] **IRQ-DATA-004** 定义 DMA buffer ownership
* [x] **IRQ-DATA-005** 定义 producer/consumer ownership
* [x] **IRQ-DATA-006** 定义 cache coherency
* [x] **IRQ-DATA-007** 定义 buffer overflow policy
* [x] **IRQ-DATA-008** 定义 DMA completion notification
* [x] **IRQ-DATA-009** EventQueue 只通知“数据可用”，不承载数据本身

---

# 13. UART IRQ

作为第一个 Data IRQ reference implementation。

* [x] **UART-001** UART RX FIFO
* [x] **UART-002** UART RX ring buffer
* [x] **UART-003** UART DMA RX
* [x] **UART-004** RX ready notification
* [x] **UART-005** overflow counter
* [x] **UART-006** framing error handling
* [x] **UART-007** parity error handling
* [x] **UART-008** idle-line handling
* [x] **UART-009** ISR worst-case execution time测试
* [x] **UART-010** 高速数据压力测试

---

# 14. SPI/I2C DMA

* [x] **DMA-001** SPI DMA completion model
* [x] **DMA-002** I2C completion model
* [x] **DMA-003** buffer ownership
* [x] **DMA-004** transfer descriptor lifecycle
* [x] **DMA-005** timeout handling
* [x] **DMA-006** cancellation handling
* [x] **DMA-007** error IRQ handling
* [x] **DMA-008** DMA event 与数据 buffer 分离

---

# 15. Scheduler / Timer IRQ

Timer 不应该默认：

```text
Timer IRQ
 ↓
EventQueue
```

而应该：

```text
Timer IRQ
 ↓
timebase / scheduler
```

---

## TODO

* [x] **TIMER-001** 明确 Timer IRQ 类型
* [x] **TIMER-002** 禁止周期 tick 进入普通 EventQueue
* [x] **TIMER-003** 建立 timebase
* [x] **TIMER-004** 建立 scheduler wakeup path
* [x] **TIMER-005** 明确 RTOS tick 与 framework timer 的关系
* [x] **TIMER-006** 定义 high-resolution timer
* [x] **TIMER-007** 定义 deadline/next-due
* [x] **TIMER-008** 测试 1 kHz/10 kHz timer 压力
* [x] **TIMER-009** 测量 ISR CPU overhead

---

# 16. RTOS IRQ

* [x] **RTOS-001** 明确 SysTick ownership
* [x] **RTOS-002** 明确 PendSV ownership
* [x] **RTOS-003** 明确 SVC ownership
* [x] **RTOS-004** 明确 RTOS ISR 与 framework ISR 的边界
* [x] **RTOS-005** 禁止 framework EventQueue 接管 RTOS scheduler IRQ
* [x] **RTOS-006** 定义 ISR → task notification
* [x] **RTOS-007** 定义 ISR → semaphore/queue 的适用场景

---

# 17. Fault Path

这些不能进入普通 EventQueue：

```text
HardFault
MemFault
BusFault
UsageFault
NMI
Watchdog
Brown-out
Reset
```

---

## TODO

* [x] **FAULT-001** 定义 Fault IRQ 独立路径
* [x] **FAULT-002** CPU register capture
* [x] **FAULT-003** fault status register capture
* [x] **FAULT-004** stack frame capture
* [x] **FAULT-005** crash record
* [x] **FAULT-006** persistent crash storage
* [x] **FAULT-007** safe-state transition
* [x] **FAULT-008** watchdog reset policy
* [x] **FAULT-009** brown-out handling
* [x] **FAULT-010** reset-cause capture
* [x] **FAULT-011** Fault path 禁止依赖正常 Sys/EventQueue
* [x] **FAULT-012** fault handler 尽量避免动态内存/复杂 libc

---

# 18. EventQueue 重新定义职责

最终建议把 EventQueue 定义成：

> **Control/Event Plane，而不是 Data Plane。**

---

## TODO

* [x] **QUEUE-001** 明确 Event = fact
* [x] **QUEUE-002** Command 不通过 EventQueue
* [x] **QUEUE-003** 数据不通过 EventQueue
* [x] **QUEUE-004** Event payload 保持固定 scalar
* [x] **QUEUE-005** 禁止 raw pointer payload
* [x] **QUEUE-006** 定义 event timestamp
* [x] **QUEUE-007** 定义 event source
* [x] **QUEUE-008** 定义 event overflow
* [x] **QUEUE-009** 定义 event loss semantics
* [x] **QUEUE-010** 定义 critical/non-critical event
* [x] **QUEUE-011** 定义 queue depth sizing
* [x] **QUEUE-012** 做 queue worst-case analysis

---

# 19. ISR 安全规则

建立统一 ISR Coding Rule。

* [x] **ISR-001** ISR 不调用 app callback
* [x] **ISR-002** ISR 不调用 blocking API
* [x] **ISR-003** ISR 不 malloc/free
* [x] **ISR-004** ISR 不 printf
* [x] **ISR-005** ISR 不做复杂 protocol parsing
* [x] **ISR-006** ISR 不执行业务策略
* [x] **ISR-007** ISR 不持有长时间 spinlock
* [x] **ISR-008** ISR 不访问非 ISR-safe API
* [x] **ISR-009** ISR 执行时间有预算
* [x] **ISR-010** CI/static analysis 检查 ISR 禁止调用列表

---

# 20. Board Module 边界

今天也明确了：

> board 不是“大 BSP 上帝”。

---

## TODO

* [x] **BOARD-001** board 拥有 IRQ vector
* [x] **BOARD-002** board 拥有硬件资源
* [x] **BOARD-003** board 做 IRQ → framework/driver primitive
* [x] **BOARD-004** board 不知道 app
* [x] **BOARD-005** board 不知道 product
* [x] **BOARD-006** board 不决定业务行为
* [x] **BOARD-007** board 不暴露 app list
* [x] **BOARD-008** board 不暴露 module state
* [x] **BOARD-009** 明确 shared IRQ 模型
* [x] **BOARD-010** 明确 IRQ source mapping
* [x] **BOARD-011** 明确 IRQ enable/disable ownership

---

# 21. Shared IRQ

这个问题还没有真正落地。

* [x] **SHIRQ-001** 支持一个 IRQ 多个 peripheral
* [x] **SHIRQ-002** 定义 IRQ source probing
* [x] **SHIRQ-003** 定义 source acknowledgement
* [x] **SHIRQ-004** 防止重复 event
* [x] **SHIRQ-005** 定义 priority
* [x] **SHIRQ-006** shared IRQ 测试

---

# 22. Low Power / Wakeup

今天之前已经发现：

> 当前项目的 low-power 架构方向不错，但真正硬件低功耗闭环还不完整。

---

## TODO

### Power Policy

* [x] **PM-001** 定义 RUN
* [x] **PM-002** 定义 IDLE
* [x] **PM-003** 定义 LIGHT SLEEP
* [x] **PM-004** 定义 DEEP SLEEP

### Wake Source

* [x] **PM-005** Wake source registry
* [x] **PM-006** GPIO wake
* [x] **PM-007** RTC wake
* [x] **PM-008** Timer wake
* [x] **PM-009** UART wake
* [x] **PM-010** BLE wake
* [x] **PM-011** IMU wake
* [x] **PM-012** Touch wake

### Wake Lock

* [x] **PM-013** Wake lock manager
* [x] **PM-014** BLE TX wake lock
* [x] **PM-015** SPI transaction wake lock
* [x] **PM-016** Flash write wake lock
* [x] **PM-017** Display refresh wake lock
* [x] **PM-018** pending alarm wake lock

### Deadline

* [x] **PM-019** `next_due` scheduler API
* [x] **PM-020** 根据 next deadline 决定 sleep duration
* [x] **PM-021** event queue pending 状态参与 sleep decision

---

# 23. Low Power 生命周期

* [x] **PM-LIFE-001** suspend order
* [x] **PM-LIFE-002** board hardware suspend
* [x] **PM-LIFE-003** infra suspend
* [x] **PM-LIFE-004** app suspend
* [x] **PM-LIFE-005** resume order
* [x] **PM-LIFE-006** wake source restore
* [x] **PM-LIFE-007** clock restore
* [x] **PM-LIFE-008** peripheral restore
* [x] **PM-LIFE-009** state consistency
* [x] **PM-LIFE-010** suspend failure rollback

---

# 24. 当前 Stub / Fake Hardware 行为清理

此前已经发现部分 board low-power / hardware functions 更像：

```text
counter++
```

而不是：

```text
真实 hardware operation
```

TODO：

* [x] **HW-001** 审查所有 board low-power API
* [x] **HW-002** 删除仅 increment counter 的 fake implementation
* [x] **HW-003** 区分 host simulation 与 production BSP
* [x] **HW-004** 对真实 BSP 实现 STOP/deep sleep
* [x] **HW-005** clock gating
* [x] **HW-006** peripheral shutdown
* [x] **HW-007** RAM retention
* [x] **HW-008** wake source configuration
* [x] **HW-009** wake reason capture

---

# 25. Event Routing

当前设计：

```text
board
 ↓
event
 ↓
sys
 ↓
subscription
 ↓
app
```

这个方向可以保留。

---

## TODO

* [x] **ROUTE-001** board 只产生 event
* [x] **ROUTE-002** sys 负责 routing
* [x] **ROUTE-003** app 负责 subscription
* [x] **ROUTE-004** board 不维护 app subscription
* [x] **ROUTE-005** 明确一个 event 多 subscriber
* [x] **ROUTE-006** 明确 event ordering
* [x] **ROUTE-007** 明确 event priority
* [x] **ROUTE-008** 明确 event coalescing
* [x] **ROUTE-009** 明确 duplicate event policy

---

# 26. Event Coalescing

特别针对：

```text
UART_RX_READY
GPIO
Timer
IMU
ADC
```

避免：

```text
event
event
event
event
event
```

---

## TODO

* [x] **COALESCE-001** 定义 coalescible event
* [x] **COALESCE-002** `UART_RX_READY` 可合并
* [x] **COALESCE-003** IMU data-ready 可合并
* [x] **COALESCE-004** ADC ready 可合并
* [x] **COALESCE-005** GPIO edge 是否可合并需按语义决定
* [x] **COALESCE-006** critical event 禁止无条件合并
* [x] **COALESCE-007** 测试 event storm

---

# 27. EventQueue Backpressure

这是当前架构必须补的。

* [x] **BACKPRESSURE-001** queue full 行为
* [x] **BACKPRESSURE-002** drop-newest
* [x] **BACKPRESSURE-003** drop-oldest
* [x] **BACKPRESSURE-004** critical event reserved slots
* [x] **BACKPRESSURE-005** overflow counter
* [x] **BACKPRESSURE-006** overflow diagnostics
* [x] **BACKPRESSURE-007** queue depth sizing methodology
* [x] **BACKPRESSURE-008** worst-case burst analysis

---

# 28. 测试矩阵

最终至少需要：

```text
App Unit Test
Sys Test
Board IRQ Test
Product Integration Test
Hardware/Renode Test
Stress Test
```

---

## TODO

* [x] **TEST-001** App 单元测试
* [x] **TEST-002** Sys scheduling test
* [x] **TEST-003** Event routing test
* [x] **TEST-004** Event queue overflow test
* [x] **TEST-005** ISR → event test
* [x] **TEST-006** UART ISR → ring buffer test
* [x] **TEST-007** DMA completion test
* [x] **TEST-008** timer test
* [x] **TEST-009** fault handler test
* [x] **TEST-010** low-power/wakeup test
* [x] **TEST-011** Renode IRQ test
* [x] **TEST-012** hardware-in-loop IRQ test
* [x] **TEST-013** event storm test
* [x] **TEST-014** queue saturation test
* [x] **TEST-015** ISR latency test
* [x] **TEST-016** scheduler latency test

---

# 29. CI 架构检查

* [x] **CI-ARCH-001** Module ID collision checker
* [x] **CI-ARCH-002** Event ID collision checker
* [x] **CI-ARCH-003** app dependency boundary checker
* [x] **CI-ARCH-004** board → app dependency checker
* [x] **CI-ARCH-005** ISR forbidden API checker
* [x] **CI-ARCH-006** ISR → app callback detection
* [x] **CI-ARCH-007** Event payload pointer detection
* [x] **CI-ARCH-008** CMake ↔ product composition consistency
* [x] **CI-ARCH-009** module dependency cycle detection
* [x] **CI-ARCH-010** mandatory module validation

---

# 30. Documentation / ADR

建议最终至少补下面几个 ADR。

* [x] **ADR-001** Module ID Ownership
* [x] **ADR-002** Event ID Ownership
* [x] **ADR-003** Module Lifecycle
* [x] **ADR-004** Product Composition Root
* [x] **ADR-005** Module Dependency Model
* [x] **ADR-006** Interrupt Delivery Model
* [x] **ADR-007** Event vs Data Plane
* [x] **ADR-008** ISR Coding Rules
* [x] **ADR-009** EventQueue Backpressure
* [x] **ADR-010** Low Power / Wakeup Model

---

# 31. 推荐实施顺序

不要一次把所有东西推翻重写。

## Phase 1 —— 先解决全局耦合

* [x] `modules.h` 分布式化
* [x] `events.h` 分布式化
* [x] Module/Event CI validator

---

## Phase 2 —— 解决 `construct/apps[]`

* [x] `construct → init`
* [x] Module Descriptor
* [x] lifecycle
* [x] dependency
* [x] `edge_product_t`
* [x] 隐藏 runtime 内部 `apps[]`

---

## Phase 3 —— 重新定义中断架构

这是我认为**今天讨论后最应该新增的一项工作**：

```text
             IRQ
              │
      ┌───────┼────────┬──────────┐
      ▼       ▼        ▼          ▼
    Event    Data    Scheduler   Fault
      │       │        │          │
    EventQ   DMA      Timebase   Crash
      │       │        │          │
      └───────┴────────┴──────────┘
                    │
                   SYS
                    │
                   APP
```

* [x] ISR classification
* [x] Event path
* [x] Data path
* [x] Scheduler path
* [x] Fault path
* [x] ISR coding rules

---

## Phase 4 —— EventQueue 工程化

* [x] backpressure
* [x] overflow
* [x] coalescing
* [x] priority
* [x] timestamp
* [x] stress test

---

## Phase 5 —— Low Power

* [x] PM policy
* [x] Wake Source
* [x] Wake Lock
* [x] Deadline
* [x] suspend/resume
* [x] real hardware implementation

---

## Phase 6 —— CI 闭环

最终 CI 应该能够回答：

```text
这个 Module ID 是否冲突？
这个 Event ID 是否冲突？
这个 app 是否越界依赖？
这个 ISR 是否调用了非法 API？
这个 IRQ 是否走了正确的 delivery path？
EventQueue 是否可能溢出？
DMA buffer 是否存在 ownership 问题？
产品装配是否完整？
Module dependency 是否有环？
所有产品是否都能构建？
```

---

# 最终架构原则

今天讨论后，我认为这个项目应该把下面这几句话作为非常重要的架构原则：

```text
1. Module ID 是身份，不是数组索引。

2. Event ID 是契约，不应该成为中央耦合点。

3. Product 决定“装什么”，Runtime 决定“怎么运行”。

4. apps[] 可以存在于 Runtime 内部，
   但不应该成为 Product 与 Runtime 之间的架构接口。

5. ISR 不是只有一条出口。

6. EventQueue 是 Event/Control Plane，
   不是 Data Plane。

7. Event 传递“事实”，
   RingBuffer/DMA 承载“数据”。

8. Timer/Scheduler IRQ 不应该变成普通业务 Event。

9. Fault IRQ 不应该依赖正常 EventQueue。

10. Board 拥有硬件和 IRQ，
    但不知道 App。

11. Sys 负责运行时编排，
    但不应该成为所有异步活动的垃圾桶。

12. Product 是 Composition Root，
    不应该包含业务逻辑。
```

**其中优先级最高的三个重构，我会定为：**

```text
P0  modules.h / events.h 全局耦合消除
P0  xxx_construct() + apps[] → Product Composition + Module Lifecycle
P0  ISR → EventQueue 单一路径 → 四类 Interrupt Delivery Model
```

这三个完成后，再做低功耗、DMA、EventQueue backpressure 等，会比较顺。
