# 模块化系统 TODO（历史存档）

> **已被 [`docs/adr.md`](docs/adr.md) 取代。** `docs/adr.md`（D1-D85）是本项目唯一的决策源；
> 本文件保留 D1-D45 冻结子集与早期实施清单，仅作存档、不再更新。
> 决策与实现的唯一差异视图见 [`docs/adr-conformance.md`](docs/adr-conformance.md)。

## 1. 总体架构

三轴软件产品线（SPL）：

```text
                 产品族轴                 板子轴
              sys/<family>             board/<board>
                    \                     /
                     \                   /
                    product/<name>
                  组合根 / Composition Root
                    /                   \
                   /                     \
            app/<name>              infra/<name>
                   \                     /
                    \                   /
                     edge_module
```

- `sys`：每个产品族一个运行时调度/策略实现。
- `board`：每块板一个；拥有 IRQ 向量和板级硬件动作，但不认识业务 app。
- `app`：所有产品共享的业务模块；只依赖框架契约和自己定义的消费者接口。
- `infra`：具体基础设施实现；不依赖 app 业务类型。
- `product`：唯一组合根；选择 board/sys/infra，构造 app，连接 adapter，形成最终产品。

## 2. 冻结决策 D1-D45

| ID | 决策 |
|---|---|
| D1 | app 可以依赖 `edge_module`；零具体实现依赖。 |
| D2 | 每产品族一个 sys 实现；变体不复制公共 loop 骨架。 |
| D3 | board 只转发中断并执行硬件动作，不提供业务设备访问。 |
| D4 | 事件号中央分配，防冲突。 |
| D5 | 显式装配；放弃链接段自动注册；`main()` 显式构造 app。 |
| D6 | 构造注入；`xxx_init(self, deps)`，不使用 service locator。 |
| D7 | 不使用 manifest 作为产品事实来源；CMake target + `main()` 为事实来源。 |
| D8 | monorepo。 |
| D9 | board 负责硬件动作，sys 负责软件策略，app 负责模块状态。 |
| D10 | 保留多核接口，但当前不实现跨核机制。 |
| D11 | sys 按 priority 调度，同 priority 用 `module_id` 升序 tie-break。 |
| D12 | 设备实例/适配器由组合根提供。 |
| D13 | 允许产品私有 port；接口跟随消费者。 |
| D14 | 使用者定义接口；infra 只提供具体 API；adapter 位于 product/glue。 |
| D15 | `edge_module` 极薄：生命周期契约 + event queue + util。 |
| D16 | event 只表达已发生的事实；命令走直接 contract 调用。 |
| D17 | board/ISR 只 push 到注入的 event sink；sys 负责路由。 |
| D18 | event payload 固定标量 `{id, source, arg0, arg1}`，禁止原生指针。 |
| D19 | 全层统一 `edge_status_t`：0 成功，负值 errno 风格。 |
| D20 | 时间通过 `clock_port_t` 注入，至少支持 monotonic tick + wall clock。 |
| D21 | 运行期零分配；调用者提供对象存储。 |
| D22 | 多个小接口，按 app 实际需要注入。 |
| D23 | 存储最小抽象为 KV：key -> bytes；块/流另立 port。 |
| D24 | 设备按能力拆窄接口，例如 byte reader/writer、relay output。 |
| D25 | 双工具链：CMake 驱动 GCC 与 IAR/iccarm；`.ewp` 仅 IDE 调试。 |
| D26 | `edge_add_product(name, family, board, apps...)` 统一生成产品 target。 |
| D27 | 每模块独立 CMake target + 精确 include 路径隔离。 |
| D28 | CMake + 静态断言执行重复 ID、事件号、清单和层依赖校验。 |
| D29 | 统一 `edge_` / `sys_` / `board_` / `app_` 前缀和事件号段。 |
| D30 | 公共工具归 `edge_util`，使用 header-only，避免 app 引入其它实现。 |
| D31 | 唯一事件号表 `edge/events.h`，编译期唯一性检查。 |
| D32 | `main()` 显式 `sys_subscribe(event_id, app)`。 |
| D33 | event sink 入队时写 monotonic timestamp，不使用序号。 |
| D34 | SDK 分发单独立项：契约版本、打包、兼容矩阵。 |
| D35 | 合规单独立项；当前只冻结认证边界。 |
| D36 | 模块结构：`include/<name>/` 公共头 + `src/` 私有实现。 |
| D37 | CI 解析产品 map，执行 Flash/RAM 预算。 |
| D38 | 可复现构建：固定工具链版本，记录 commit/toolchain hash。 |
| D39 | CI 对合法 family × board 组合全量构建、单测、双工具链。 |
| D40 | ABI/contract 版本只增不改、绝不复用，版本进入 descriptor。 |
| D41 | 日志/断言/trace 使用注入 `log_port_t`；断言可编译期剔除。 |
| D42 | 持久化 schema 使用版本号 + migration。 |
| D43 | C++ 互操作只保证公共头可被 `extern "C"` 正确 include。 |
| D44 | framework 层零架构相关代码；IRQ/寄存器只在 board/infra。 |
| D45 | 认证核心为 edge_module + sys + board + infra；app 在核心外，非运行期沙箱。 |

## 3. 知识边界

| 层 | 允许知道 | 禁止知道 |
|---|---|---|
| app | 自己的接口、edge_module、标准 C | 其它 app、具体 infra、寄存器、产品族、板子 |
| sys | 产品族策略、app 列表、注入 port | 具体寄存器、具体板型 |
| board | MCU/板级寄存器、pin/clock/IRQ | 业务、产品族、app 私有类型 |
| infra | 自己的驱动/HAL | app 业务类型 |
| product | 所有具体类型，用于构造/连接 | 业务逻辑 |

### 强制零依赖规则

- [x] N1 app 不 include 其它 app。
- [x] N2 app 不 include 具体 infra。
- [x] N3 app 不访问寄存器/外设地址。
- [x] N4 app 不依赖 RTOS、线程或动态分配。
- [x] N5 app 不使用 printf/malloc 等重 libc；确有需要时用 `N5-allow` 注释显式白名单。

## 4. 目录标准

```text
edge_module/
    include/edge/module.h
    include/edge/events.h
    include/edge/util/*.h
    src/module.c
    src/event.c
app/<name>/
    include/<name>/*.h
    src/*.c
sys/<family>/
    include/<family>/*.h
    src/*.c
board/<board>/
    include/<board>/*.h
    src/*.c
infra/<name>/
    include/<name>/*.h
    src/*.c
product/<name>/
    glue/*.c
    main.c
    apps/
```

依赖方向：

```text
app     -> edge_module
sys     -> edge_module
board   -> edge_module
infra   -> edge_module
product -> 全部
```

不存在独立 `contracts/` 层；消费者接口随 app。

## 5. 生命周期与调度

目标生命周期：

```text
construct -> init -> running -> suspend/resume -> power_off -> deinit
```

调度规则：

1. `main()` 显式给出 app 数组。
2. sys 不负责构造 app。
3. sys 按 priority 排序。
4. priority 相同按 module_id 升序。
5. priority 只表示顺序，不表示 required。
6. required module 集合单独校验。
7. foreground loop 不允许阻塞式长操作。
8. app 的 poll 必须是 bounded / cooperative step。
9. 关停默认逆序；最终策略仍由 sys 生命周期设计固定。

待实现/确认：周期分频、执行预算、故障隔离、idle/低功耗、完整 callback 集合。

## 6. 事件模型

```text
hardware IRQ
    |
    v
board ISR
    |  最小采集/清 IRQ
    v
injected event sink
    |  写 timestamp + bounded queue
    v
sys router
    |  event id subscription
    v
app on_event
```

固定 event：

```c
struct edge_event {
    uint32_t id;
    uint32_t source;
    uint32_t arg0;
    uint32_t arg1;
    uint64_t timestamp;
};
```

规则：

- ISR 不调用 app callback。
- ISR 不携带栈指针/裸指针作为 event payload。
- queue 满时采用固定策略并累计 overflow/drop counter。
- 同一个 event 可以有多个 subscriber。
- `main()` 显式订阅。
- app 发布事实事件必须通过明确的 sys/event port；命令不伪装成事件。

## 7. Port / Adapter

消费者定义接口：

```c
/* app/dlt645/include/dlt645/dlt645.h */
typedef struct {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
} dlt645_storage_if;

int dlt645_init(dlt645_t *self, const dlt645_storage_if *storage);
```

infra 提供自己的具体 API；product/glue 做适配：

```c
static edge_status_t storage_read(void *self, uint32_t key,
                                  void *buf, size_t len)
{
    return flash_read(self, key, buf, len);
}
```

禁止：

- global service locator
- `get_service(id)` 隐藏依赖
- app include infra implementation header
- 大而全的 `services` 结构体；依赖应按 app 实际需要拆成小接口

## 8. 错误 / 时间 / 内存

### 错误

统一 `edge_status_t`：

```text
0          success
negative   errno-style failure
```

必须明确各层允许返回的错误集合，并避免把业务错误与基础设施错误混成一个无语义整数。

### 时间

注入：

```c
typedef struct {
    uint64_t (*monotonic_ticks)(void *self);
    uint64_t (*wall_time)(void *self);
    void *self;
} clock_port_t;
```

### 内存

- 运行期零 malloc/free。
- app 对象由调用者提供。
- stack/static/arena 生命周期由组合根决定。
- 所有固定队列容量显式配置。

## 9. 产品组合

产品唯一事实来源：

1. `product/<name>/CMakeLists.txt`：编译哪些组件。
2. `product/<name>/main.c`：实际构造/装配哪些 app。

不得引入 manifest 作为第三套事实来源。

推荐 API：

```cmake
edge_add_product(
    name meter_a
    family meter
    board example
    apps dlt645 relay
)
```

组合根只做：

```text
select -> construct -> adapt -> connect -> start
```

禁止业务 if/else 泄漏进 glue/main。

## 10. Board

board module 的边界：

- 拥有 IRQ vector。
- 清硬件 IRQ 状态。
- 捕获最小必要硬件信息。
- push 到注入 sink。
- 提供硬件安全动作（reset/watchdog/low-power hardware action）。

禁止：

- 调 app callback。
- 认识 app 类型。
- 保存 app 列表。
- 做产品族策略。
- 暴露业务模块状态。

设备访问由 infra/driver 提供。

## 11. 构建与 CI/CD

CI 是工程质量门，不只是“能编译”。

### 主 CI matrix

- GCC Debug
- GCC Release
- Clang Debug
- Clang Release
- GCC ASan + UBSan
- Clang ASan + UBSan

每个组合：configure -> build -> ctest。

### 静态质量

- `-Wall -Wextra -Wpedantic`
- CI 默认 `-Werror`
- clang-tidy
- cppcheck
- architecture dependency guard
- event/module ID collision check
- app include boundary check

### Coverage

- GCC + Debug + CMocka
- gcovr
- coverage artifact

### 产品矩阵

CI 必须最终扩展到合法：

```text
family × board × app-set
```

全量构建。

### 双工具链

GCC 与 IAR/iccarm 均由 CMake 驱动；`.ewp` 不成为第二构建事实来源。

### 可复现

每个发布构建记录：

```text
source commit
compiler version
CMake version
SDK/toolchain version
build flags
```

### 体积门禁

产品构建生成 map 后检查：

```text
FLASH <= budget
RAM   <= budget
```

超预算 CI 失败。

## 12. 测试策略

| 层 | 测试 |
|---|---|
| app | CMocka + fake port；不需要硬件 |
| sys | fake apps；排序/事件/预算/降级 |
| board | Renode/HIL；IRQ -> event |
| product | 集成测试；装配/依赖完整性 |
| matrix | 合法 family × board 全量构建 |
| contract | 版本演进兼容性 |

反例测试必须存在：故意 include infra header，CI 必须失败。

## 13. ABI / 分发 / 认证

ABI/contract：

- 版本只增不改。
- 版本号不复用。
- descriptor 中携带版本。
- ABI 变更必须更新兼容矩阵。

认证边界：

```text
CORE = edge_module + sys + board + infra
APP  = core 外部资产
```

该边界是源码/变更控制 + ABI + 固件签名边界，不意味着 app 有运行期沙箱。

运行期可加载第三方模块属于未来独立架构，不与当前静态装配模型混合。

## 14. 当前实施状态

### 已落地

- [x] monorepo 基础目录。
- [x] 显式 app composition root 示例。
- [x] constructor injection 示例。
- [x] sys priority + module_id tie-break。
- [x] required module 校验接口。
- [x] board IRQ -> injected sink 示例。
- [x] CMocka host tests。
- [x] app dependency boundary CI。
- [x] GCC/Clang Debug/Release matrix。
- [x] ASan/UBSan matrix。
- [x] clang-tidy/cppcheck。
- [x] coverage job。
- [x] 唯一 `edge/events.h` 与编译期重复事件号检查。
- [x] `sys_subscribe()` + 多 subscriber 路由，以及 `sys_unsubscribe()`。
- [x] event sink 入队 monotonic timestamp。
- [x] bounded queue overflow/drop 统计与背压策略。
- [x] `clock_port_t` / `log_port_t` 注入。
- [x] 中央 `edge/errors.h` 统一 `edge_status_t` 与 `EDGE_ERR` 号段。
- [x] `edge_add_product()` 产品矩阵 + 合法 family×board 白名单。
- [x] CMake/main app 清单一致性检查。
- [x] app target 精确 include 隔离，反例 CI 验证失败（含 N3/N5）。
- [x] 重复 module ID 编译期检查（`edge/modules.h` + `check_module_ids.py`）。
- [x] 产品 map Flash/RAM budget gate（总量 + 分层）。
- [x] reproducible-build metadata + 逐字节比对。
- [x] scheduler `period`/`budget` + idle 钩子 + fault isolation + stats。
- [x] 生命周期 callback 最终定义（`poll`/`on_event`/`suspend`/`resume`/`power_off`）。
- [x] runner 内 `sys_publish`（延后投递 + 深度上限 + drop 计数）。
- [x] 非致命 init 失败跳过并记录，`fatal` 模块才整机失败。

### 下一阶段必须实现

- [ ] 把 `edge_module` 收缩为 D15 的最终极薄契约（D51 与当前结构体仍有分歧）。
- [ ] 完成 GCC + IAR/iccarm CMake 双工具链路径（工具链文件已备，未进 CI）。
- [ ] 完成 Renode/HIL board IRQ 测试。
- [ ] family × board × app-set 全量枚举矩阵（当前为白名单 + 3 个合法产品）。
- [ ] scheduler 周期分频 / 每模块高水位诊断。
- [ ] 完成 watchdog / low-power 板级策略（idle 钩子已有，`board_enter_low_power` 未接）。
- [ ] 多生产者 SPSC 队列（当前单队列 + 注入 IRQ guard）。
- [ ] 完成 ABI/contract version compatibility matrix。

## 15. 明确禁止的反模式

1. 链接段自动注册成为产品装配来源。
2. service locator / `get_service()` 隐藏依赖。
3. app 直接 include infra。
4. app 直接摸寄存器。
5. board 调业务 callback。
6. sys 负责构造具体 app。
7. product glue 写业务逻辑。
8. app 间直接链接形成隐式依赖。
9. event/command 语义混用。
10. event ID 散落定义或复用旧 ID。
11. 运行期 malloc/free。
12. 用 manifest 再建立一套与 CMake/main 平行的产品事实来源。
13. 为了“模块化”重新引入线程/RTOS 依赖；当前模型是 foreground/background cooperative execution。

## 16. 近期实施顺序

```text
P0  framework contract / event model
P1  sys subscription + lifecycle + scheduler
P2  app isolation + CMake target discipline
P3  product matrix + composition validation
P4  board IRQ / Renode-HIL
P5  clock/log/error/memory cross-cutting ports
P6  map budget + reproducible build + dual toolchain
P7  ABI/distribution compatibility
P8  compliance / certification workstream
```

**原则：先把边界和工程机制做硬，再增加业务模块数量。**
