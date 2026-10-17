# 架构实操推演与方法论：Product 与 Module 工程师双重视角

本文档旨在统一嵌入式前后台分层架构（8-layer modular architecture）在日常研发中的工程实践规范。从 **Product 工程师（系统组合根装配者）** 与 **Module / App 工程师（领域业务逻辑实现者）** 的双重视角出发，系统推演“做什么”与“怎么做”，并阐明自动化架构门禁（Architecture Guards）如何为各项设计原则护航。

---

## 核心设计原则总览

在深入具体视角之前，先明确指导本项目代码设计的八大核心原则：

| 架构原则 | 领域驱动设计 (DDD) / Clean Architecture 含义 | 本系统（嵌入式 C11）中的具体工程落地 |
| :--- | :--- | :--- |
| **有界上下文 (Bounded Context)** | 划定领域边界，不同上下文模型严格隔离 | `app/<name>` 独立目录，禁止包含对等应用私有头文件与底层驱动，仅通过事件和标准数据交互 |
| **聚合与聚合根 (Aggregate & Root)** | 封装领域实体与值对象，通过聚合根保障一致性与不变性 | 应用主结构体（如 `alarm_app_t`）作为根，域内设置结构体必须配有纯校验函数、非 void 设置器与只读快照获取函数 |
| **调用方分配 (User Alloc / Caller-Owned)** | 对象生命周期由调用者/外部容器显式控制 | 运行时禁止调用 `malloc`/`free`；模块提供 `<X>_STORAGE_SIZE`/`ALIGN` 或外部传入指针，生命周期由上层静态持有 |
| **组合根 (Composition Root)** | 系统装配逻辑集中于单一入口，统一完成依赖注入 | `product/<name>/main.c` 显式实例化所有静态结构体、管线连接、适配器绑定与调度器启动，无业务逻辑 |
| **消费者定义接口 (Consumer-Defined Ports)** | 依赖倒置：依赖方定义所需接口签名，被依赖方适配实现 | `app/<name>/include/<name>/ports.h` 定义 `*_if_t` 结构体，显式包含 `void *self;` 手动 this 指针，回调首参为 `void *` |
| **依赖倒置 (DIP)** | 高层模块不直接依赖低层模块，二者均依赖抽象 | 应用层依赖 Port 抽象，不依赖 `infra/`、`board/` 或 `soc/`；在产品层 `glue` 完成适配器构造与函数指针绑定 |
| **开闭原则 (OCP)** | 对扩展开放，对修改关闭 | 引入新传感器/硬件或新业务特性只需新增驱动适配器或领域模块，核心业务状态机与事件调度器代码无需修改 |
| **单一职责 (SRP)** | 一个模块只负责单一业务能力或系统角色 | 业务计算归 `app/`，硬件寄存器归 `soc/`，引脚与中断归 `board/`，调度归 `sys/`，装配归 `product/` |

---

## 视角一：Module (App) 工程师的方法论

### 1. 工程师定位与职责边界
- **身份定位**：业务领域专家。负责实现纯净的业务逻辑、状态机迁移和协议编解码。
- **职责范围**：`app/<name>/` 目录。
- **核心承诺**：
  - 零底层硬件依赖：不包含 `soc/`、`board/`、`infra/` 甚至 RTOS 头文件。
  - 零动态内存分配：不使用 `malloc`、`free`、`calloc`，所有内存由外部调用方传入。
  - 领域不变性保证：聚合根必须校验所有状态变更入参，防止非法状态渗入领域。

---

### 2. Module 工程师“做什么” (What To Do)

1. **识别有界上下文与聚合边界**：
   - 确定业务实体与值对象：例如闹钟应用中的 `alarm_settings_t` 是纯值对象，`alarm_app_t` 是聚合根。
   - 确定模块 ID 与优先级范围：向 `edge_module/include/edge/modules.h` 申请全局唯一的 `EDGE_MOD_<NAME>`。
2. **定义消费者接口（Consumer-Defined Ports）**：
   - 如果业务需要与外界（持久化、时钟源、马达、显示）交互，必须在 `include/<name>/ports.h` 中定义纯抽象接口结构体。
   - 每个接口结构体必须包含 `void *self;`，所有回调函数第一个参数必须为 `void *`。
3. **固化聚合根不变性（Aggregate Invariants）**：
   - **校验器**：定义纯函数 `bool <domain>_validate_<name>(const <domain>_<name>_t *data)`，校验合法范围（如小时 0-23、分钟 0-59）。
   - **非 void 状态修改器**：任何改变领域受限状态的函数（如 `_set_time`）必须返回 `edge_status_t` 或 `bool`，拒绝越界输入，禁止使用 `void` 吞没错误。
   - **只读快照访问器**：提供 `<domain>_get_<name>(const <domain>_t *self, ...)` 传出只读副本，禁止外部直接读写聚合根内部私有字段。
4. **接入前后台生命周期协议**：
   - 聚合根内部内嵌 `edge_module_t module;`。
   - 实现 `poll()`（有界协作式步进）、`on_event()`（标量事实响应）、`power_off()`。
5. **纯标量事件广播**：
   - 发布事件时，仅使用 24 字节固化大小的 `edge_event_t`，通过 `edge_event_sink_push_isr()` 发送标量事实（`arg0`, `arg1`），禁止传递裸指针。

---

### 3. Module 工程师“怎么做” (How To Do It: 实施流程与 C11 模板)

#### 步骤 1：定义消费者接口 `include/<app>/ports.h`
```c
#ifndef APP_ALARM_PORTS_H
#define APP_ALARM_PORTS_H

#include "edge/errors.h"
#include <stdbool.h>
#include <stdint.h>

/* 领域值对象：持久化设置 */
typedef struct alarm_settings {
    uint8_t hour;              /* 0..23 */
    uint8_t minute;            /* 0..59 */
    bool enabled;
    uint8_t recurrence;
} alarm_settings_t;

/* 消费者定义接口 1：存储接口 */
typedef struct alarm_storage_if {
    void *self;
    edge_status_t (*load)(void *self, alarm_settings_t *out_settings);
    edge_status_t (*save)(void *self, const alarm_settings_t *settings);
} alarm_storage_if_t;

/* 消费者定义接口 2：蜂鸣/振动警报接口 */
typedef struct alarm_alert_if {
    void *self;
    edge_status_t (*trigger)(void *self, uint8_t pattern);
    edge_status_t (*cancel)(void *self);
} alarm_alert_if_t;

#endif /* APP_ALARM_PORTS_H */
```

#### 步骤 2：定义领域聚合根与不变性契约 `include/<app>/<app>.h`
```c
#ifndef APP_ALARM_H
#define APP_ALARM_H

#include "alarm/ports.h"
#include "edge/errors.h"
#include "edge/event.h"
#include "edge/module.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct alarm_app {
    edge_module_t module;
    const alarm_storage_if_t *storage;
    const alarm_alert_if_t *alert;

    /* 内部封装的领域状态（只读快照暴露，禁止外部直连） */
    alarm_settings_t settings;
    bool is_alerting;
} alarm_app_t;

/* 聚合根不变性校验器（纯函数） */
bool alarm_validate_settings(const alarm_settings_t *settings);

/* 构造函数：注入消费者接口指针，生命周期由调用方持有 */
void alarm_construct(alarm_app_t *self, uint32_t module_id, uint32_t priority,
                     const alarm_storage_if_t *storage, const alarm_alert_if_t *alert);

edge_status_t alarm_init(alarm_app_t *self);
edge_status_t alarm_shutdown(alarm_app_t *self);

/* 状态读取快照 */
edge_status_t alarm_get_settings(const alarm_app_t *self, alarm_settings_t *out_settings);

/* 有界修改器：必须返回状态码，严禁 void */
edge_status_t alarm_set_time(alarm_app_t *self, uint8_t hour, uint8_t minute);
edge_status_t alarm_enable(alarm_app_t *self, bool enable);

#ifdef __cplusplus
}
#endif

#endif /* APP_ALARM_H */
```

#### 步骤 3：实现领域业务逻辑 `src/<app>.c`
```c
#include "alarm/alarm.h"
#include <string.h>

bool alarm_validate_settings(const alarm_settings_t *settings) {
    if (settings == NULL) {
        return false;
    }
    if (settings->hour >= 24u || settings->minute >= 60u) {
        return false; /* 保护领域不变性 */
    }
    return true;
}

edge_status_t alarm_set_time(alarm_app_t *self, uint8_t hour, uint8_t minute) {
    if (self == NULL) {
        return EDGE_EINVAL;
    }
    if (hour >= 24u || minute >= 60u) {
        return EDGE_EINVAL; /* 强校验，拒绝非法修改 */
    }
    self->settings.hour = hour;
    self->settings.minute = minute;
    if (self->storage != NULL && self->storage->save != NULL) {
        (void)self->storage->save(self->storage->self, &self->settings);
    }
    return EDGE_OK;
}

edge_status_t alarm_get_settings(const alarm_app_t *self, alarm_settings_t *out_settings) {
    if (self == NULL || out_settings == NULL) {
        return EDGE_EINVAL;
    }
    *out_settings = self->settings; /* 传出只读副本 */
    return EDGE_OK;
}
```

---

### 4. Module 工程师禁忌清单 (Anti-Patterns)
- ❌ **直接包含硬件头文件**：在 `app/` 中 `#include "infra/..."`、`#include "board/..."` 或 `#include "FreeRTOS.h"`（被 `check_app_isolation.py` 拦截）。
- ❌ **动态内存申请**：使用 `malloc()`、`calloc()`、`free()`（被 `check_no_dynamic_memory.py` 和 `check_consumer_ports.py` 拦截）。
- ❌ **对等应用私相授受**：在应用 A 中直接 `#include "b/b.h"` 访问应用 B 的结构体或调用函数（被 `check_layer_dependencies.py` 拦截）。应用间通讯只能通过总线发布标量事件。
- ❌ **隐式吞没错误的 void Setter**：定义 `void alarm_set_time(...)`，非法参数无法通知调用方（被 `check_aggregate_invariants.py` 拦截）。
- ❌ **缺失校验器的裸配置**：定义了领域设置结构体却未声明 `*_validate_*` 函数（被 `check_aggregate_invariants.py` 拦截）。

---

## 视角二：Product 工程师的方法论

### 1. 工程师定位与职责边界
- **身份定位**：系统架构师与装配总工程师。负责将通用模块在特定电路板（Board）和产品需求下装配成可运行固件。
- **职责范围**：`product/<name>/` 目录（`main.c`, `glue.h`, `glue.c`）。
- **核心承诺**：
  - 纯组合根（Composition Root）：零领域业务算法，仅做实例化、依赖倒置适配器实现与管线绑定。
  - 静态生命周期保证：所有应用实例、适配器状态和队列在 `main()` 静态或首帧栈空间显式申请，生命周期长于整个固件运行期。
  - 事件管线完整连接：凡是产出领域事件的服务，必须接入有效事件槽（Event Sink），禁止 `NULL` 截断。
  - 拒绝黑板式后门耦合：Glue 状态禁止捆绑聚合多个对等业务应用指针。

---

### 2. Product 工程师“做什么” (What To Do)

1. **唯一物理板卡绑定 (Product-to-Board Binding)**：
   - 在 `CMakeLists.txt` 中通过 `edge_add_product(<name> BOARD <board> FAMILY <family>)` 完成一对一物理固件绑定（受 `check_product_board_binding.py` 校验）。
2. **实现消费接口适配器（Port Adapter Implementation）**：
   - 针对各应用定义的 `*_if_t` 接口，提供静态适配器工厂函数 `product_<name>_make_<port>(port_t *out, glue_state_t *glue)`。
   - 将驱动（`infra/`）或板级能力（`board/`）的函数包装成符合应用签名的回调，将 `glue_state` 赋给 `port->self`。
3. **管线事件槽完整注入 (Event Pipeline Injection)**：
   - 为天气、导航、通知、固件验证、UI 等事件产出模块传入真实的 `edge_event_sink_t *`，确保事件进入系统事件总线。
4. **多应用数据跨界解耦（Anti-Backboard Rule）**：
   - 若 UI 或远程报文需要聚合展示多应用状态（如时间、步数、心率），**严禁**在 `glue_state` 中保存 `watch_time_t *`、`step_counter_t *`、`heart_rate_t *` 并直接跨应用读取。
   - **正解**：在 Glue 中实现一个轻量级的 `edge_module_t telemetry_module`，订阅 `EDGE_EVT_WATCH_TIME_TICK`、`EDGE_EVT_STEP_COUNTER_STEP` 等事件，在 `on_event` 中缓存纯数据到快照结构体中供 UI 读取。

---

### 3. Product 工程师“怎么做” (How To Do It: 实施流程与 C11 模板)

#### 步骤 1：设计胶水状态与适配器声明 `product/<name>/glue.h`
```c
#ifndef PRODUCT_PINETIME_GLUE_H
#define PRODUCT_PINETIME_GLUE_H

#include "alarm/alarm.h"
#include "edge/module.h"
#include "watch_time/watch_time.h"

typedef struct pinetime_glue_state {
    /* 硬件适配器状态 */
    uint32_t flash_storage[1024];

    /* 正确做法：遥测适配器模块，通过订阅事件缓存只读状态，不存对等应用指针 */
    edge_module_t telemetry_module;
    alarm_settings_t cached_alarm;
    watch_datetime_t cached_time;
} pinetime_glue_state_t;

/* 适配器工厂函数声明 */
void pinetime_make_alarm_storage_port(alarm_storage_if_t *out, pinetime_glue_state_t *state);
edge_module_t *pinetime_glue_telemetry_module(pinetime_glue_state_t *state);

#endif /* PRODUCT_PINETIME_GLUE_H */
```

#### 步骤 2：实现适配器与事件订阅 `product/<name>/glue.c`
```c
#include "glue.h"

static edge_status_t alarm_storage_save_impl(void *self, const alarm_settings_t *settings) {
    pinetime_glue_state_t *glue = (pinetime_glue_state_t *)self;
    if (glue == NULL || settings == NULL) {
        return EDGE_EINVAL;
    }
    glue->cached_alarm = *settings;
    return EDGE_OK;
}

void pinetime_make_alarm_storage_port(alarm_storage_if_t *out, pinetime_glue_state_t *state) {
    if (out == NULL) return;
    out->self = state;
    out->save = alarm_storage_save_impl;
}

static edge_status_t telemetry_on_event(edge_module_t *mod, const edge_event_t *ev) {
    pinetime_glue_state_t *glue = (pinetime_glue_state_t *)mod->private_data;
    if (ev->id == EDGE_EVT_WATCH_TIME_TICK) {
        glue->cached_time.second = (uint8_t)ev->arg0;
    }
    return EDGE_OK;
}

edge_module_t *pinetime_glue_telemetry_module(pinetime_glue_state_t *state) {
    state->telemetry_module.module_id = 0x7E01u;
    state->telemetry_module.priority = 250u;
    state->telemetry_module.on_event = telemetry_on_event;
    state->telemetry_module.private_data = state;
    return &state->telemetry_module;
}
```

#### 步骤 3：组合根装配全貌 `product/<name>/main.c`
```c
#include "edge/event.h"
#include "glue.h"
#include "watch/sys.h"

int main(void) {
    /* 1. 静态持有产品上下文（调用方分配） */
    static pinetime_glue_state_t glue_state;
    static edge_event_t event_ring[32];
    edge_event_queue_t event_queue;
    edge_event_sink_t event_sink;

    /* 初始化核心事件队列与管线汇 Polyfill */
    edge_event_queue_init(&event_queue, event_ring, 32u);
    edge_event_sink_init(&event_sink, &event_queue);

    /* 2. 构造消费者适配器 */
    alarm_storage_if_t alarm_storage;
    pinetime_make_alarm_storage_port(&alarm_storage, &glue_state);

    /* 3. 构造应用实例（传入依赖与事件槽） */
    static alarm_app_t alarm_app;
    alarm_construct(&alarm_app, EDGE_MOD_ALARM, 50u, &alarm_storage, NULL);
    alarm_init(&alarm_app);

    static ble_weather_t weather_app;
    /* 必须传入非空 event_sink，不可传 NULL，保证领域事件可分发 */
    ble_weather_init(&weather_app, NULL, &event_sink);

    /* 4. 组装模块列表注入调度器 */
    edge_module_t *modules[3];
    modules[0] = &alarm_app.module;
    modules[1] = &weather_app.module;
    modules[2] = pinetime_glue_telemetry_module(&glue_state);

    /* 5. 启动系统调度引擎 */
    watch_sys_t sys;
    watch_sys_init(&sys, modules, 3u, &event_queue);
    return watch_sys_run(&sys);
}
```

---

### 4. Product 工程师禁忌清单 (Anti-Patterns)
- ❌ **NULL Event Sink 破环管线**：调用 `ble_weather_init(..., NULL)` 或 `watch_ui_init(..., NULL)`，导致领域事件原地丢弃（被 `check_composition_root_wiring.py` 拦截）。
- ❌ **胶水层私设黑板 (Backdoor Blackboard)**：在 `glue_state` 中聚合多个业务应用指针（如 `time_app`, `step_app`, `hr_app`），并通过指针互相串门（被 `check_composition_root_wiring.py` 拦截）。
- ❌ **将业务逻辑写入 Glue**：在 `glue.c` 中编写温度转换公式、报警逻辑等业务规则。业务规则必须下沉到 `app/`。
- ❌ **动态任务堆栈悬挂**：在局部作用域内分配 `main()` 即将销毁的局部数组传给持久系统组件。

---

## 自动化架构门禁 (Architecture Guards) 校验矩阵

为了保证上述原则不因人员流动或工期压力而劣化，系统在 `.github/scripts/` 中部署了机器可执行的门禁脚本，并在本地 Pre-commit Hook 与 GitHub Actions CI 中严格拦截：

| 架构设计原则 | 对应的自动化检查器 | 检验规则与失效判定 (Failure Criteria) | 关联自测试反证用例 |
| :--- | :--- | :--- | :--- |
| **有界上下文与应用隔离** | [`check_app_isolation.py`](../.github/scripts/check_app_isolation.py) | 扫描 `app/` 中所有源码，若包含底层硬件、OS 寄存器、RTOS 或其他应用私有头文件，立即挂起 CI。 | `tests/guards/isolation_bad_*` |
| **应用纯洁性与零可变全局状态** | [`check_app_purity.py`](../.github/scripts/check_app_purity.py) | 1. `app/*/src/*.c` 中严禁出现文件作用域可变 static 变量（保证多实例可重入与 Caller-Owned 内存）。<br>2. 严禁直接包含底层硬件平台与板级驱动头文件。 | `tests/guards/app_purity_bad_*` |
| **依赖层级方向性 (Layer Topology)** | [`check_layer_dependencies.py`](../.github/scripts/check_layer_dependencies.py) | 校验 8 层包含矩阵：`infra -> soc`、`app -> infra`、`sys -> board` 等逆向包含立即报错。 | `tests/guards/layer_bad_*` |
| **消费者接口与 Caller-Owned 内存** | [`check_consumer_ports.py`](../.github/scripts/check_consumer_ports.py) | 1. 接口必须有 `void *self;` 且回调以 `void *` 为首参。<br>2. 运行时层严禁 `malloc`/`free`。<br>3. 声明内存大小（`*_STORAGE_SIZE`）的模块必须在头文件保持不透明指针。<br>4. 消费者接口头文件（`ports.h`）严禁包含具体层（`infra/`, `board/`, `soc/`, `pal/`）头文件（防止实现细节穿透）。 | `tests/guards/consumer_ports_bad_*` |
| **组合根装配完整性与反黑板** | [`check_composition_root_wiring.py`](../.github/scripts/check_composition_root_wiring.py) | 1. `product/*/main.c` 中事件产出服务初始化严禁传入字面量 `NULL`/`0`。<br>2. `product/*/glue.h` 中胶水状态结构体严禁聚合多个对等业务应用指针。 | `tests/guards/composition_wiring_bad_*` |
| **聚合根不变性与强状态约束** | [`check_aggregate_invariants.py`](../.github/scripts/check_aggregate_invariants.py) | 1. 凡定义设置结构体必须配有 `<domain>_validate_*` 校验器。<br>2. 必须具备 `<domain>_get_*` 快照读取器。<br>3. 时间等有界 Setter 严禁声明为 `void` 返回类型。 | `tests/guards/aggregate_invariants_bad_*` |
| **事件载荷纯标量约束** | [`check_event_payload.py`](../.github/scripts/check_event_payload.py) | 验证 `edge_event_t` 大小固定为 24 字节，字段全为标量标量事实，禁止携带裸指针。 | `tests/guards/event_payload_bad` |
| **产品与硬件单一绑定** | [`check_product_board_binding.py`](../.github/scripts/check_product_board_binding.py) | 保证每个 `product/<name>` 仅绑定一个 `board/<board>`，禁止板级覆盖。 | `tests/guards/binding_bad_*` |
| **门禁覆盖完备性** | [`check_guard_coverage.py`](../.github/scripts/check_guard_coverage.py) | 元测试门禁：验证每一个门禁脚本在 `run_guard_selftest.py` 中都有至少一个正向通过和一个反向拒收用例。 | `tests/guards/coverage_bad` |

---

## 结语：协作契约

- **Module 工程师写“纯函数与状态机”**：只需面对明确的输入（Event/Port）、清晰的不变性（Invariants）和确定的输出，所有依赖向外索取接口。
- **Product 工程师做“系统插拔与装配”**：拥有全局视角，负责生命周期分配、依赖倒置适配与事件流动连通，不插手具体业务判定。
- **CI 门禁充当“代码宪章守卫”**：任何违反上述契约的代码，均无法通过自动化门禁，从而确保架构长治久安。
