# 模块化系统

## 系统模块

1. sys module loop() 调用所有的应用模块 每个产品族一个
2. board module 硬件中断转发  每个板子一个

## 应用模块

所有的产品的所有的应用模块仓库

一般来说 要零依赖

## 胶水层 组合根

1. 选择 其它基础设施 比如存储子系统
2. 这些依赖作为组合根传递给sys_module
3. main()

---

# 深度发散

> 下面全部是在展开你上面这 8 行。每节先给"你的原话到底在说什么"，再给**备选方案**和**必须拍板的问题**。
> 记号：`?` = 待决策，`!` = 我认为的坑，`ALT` = 备选做法。

---

## 决策记录（D1–D24，全部已冻结）

| # | 问题 | 决策 | 直接约束 |
|---|---|---|---|
| D1 | 零依赖边界 | app 可以依赖 `edge_module` | 零依赖 = 不依赖其它**具体实现**，但可依赖框架契约 |
| D2 | sys module 形态 | 每个产品族一份实现 | 变体用代码表达；要防 N 份复制粘贴 |
| D3 | board module 范围 | **只转发中断**，不做设备访问 | 外设读写必须另找 infra/driver 端口 |
| D4 | 事件号/契约号 | 中央**分配表** | 需号段表 + 防撞号校验 |
| D5 | 注册方式 | **显式装配，放弃链接段自动注册**（出路 1） | `main()` 显式列出并构造每个 app |
| D6 | 服务注入 | **A：构造注入**（Go 原则下取代 C） | `app_new(deps)` 签名带类型化依赖 |
| D7 | manifest | **不要；用构建系统表达产品** | CMake target = 产品装配，成为产品组合的唯一事实来源 |
| D8 | 仓库 | **单仓库 monorepo** | 目录约定按 monorepo 定 |
| D9 | 故障/低功耗 | **board 只做硬件动作** | 软件策略归 sys module；模块状态归 app（Q9b 已答） |
| D10 | 多核 | **只留接口** | 现在不实现跨核，但事件模型不写死单核假设 |
| D11 | sys module 排序 | **优先级表** | app 带 priority，sys module 排序；需定义 tie-break |
| D12 | 设备端口 | **由组合根提供** | 适配器在组合根；接口由 app 定义（见 D14） |
| D13 | 产品私有端口 | **允许** | 接口可与产品同层；消费它的 app 仅限该产品内复用（Q12b 已答） |
| D14 | 端口接口位置 | **消费者定义（Go 原则）** | 接口在 app 侧；适配器在组合根；影响 D5/D6（见下） |
| D15 | 框架边界 | **极薄** | `edge_module` = `edge_module_t`（生命周期）+ `edge_event_queue`；装配/顺序/依赖在 main + sys |
| D16 | 事件语义 | **只有"事实"** | 命令走 contract 直接调用；不搞"一切皆事件" |
| D17 | 事件路由 | **board 只 push 到注入的 sink** | 路由由 sys module 按 event id 做；board 不认识 app（守 D3） |
| D18 | 事件载荷 | **固定标量 `{id,source,arg0,arg1}`，禁指针** | 对齐 preamble「不放原生指针」 |
| D19 | 错误模型 | **统一 `edge_status_t`（0 成功 / 负值 errno 风格）** | 需写清每层可返回的集合 |
| D20 | 时间抽象 | **注入 `clock_port_t`** | 单调 tick + 墙上时间；app 从 deps 拿 |
| D21 | 内存模型 | **调用者提供存储，运行期零分配** | `xxx_init(self, deps)`；Go `new` 在 C 的落地 |
| D22 | 接口粒度 | **多个小接口** | `init` 参数按需注入，不搞大 deps |
| D23 | 存储抽象 | **最小 KV（key → bytes）** | 块/流若需要另立端口 |
| D24 | 设备抽象 | **按能力拆窄接口** | `byte_reader`/`byte_writer`/`relay_out`…，无大 device |
| D25 | 双工具链 | **CMake 驱动 iccarm；`.ewp` 仅 IDE 调试** | 构建事实来源只有一个，避免 CMake 与 .ewp 双源漂移 |
| D26 | 产品矩阵 | **CMake function `edge_add_product(...)` 生成 target** | 不手写重复 target |
| D27 | include 隔离 | **每模块独立 target + 精确 `target_include_directories`** | app 只拿到自己 + `edge_module` + 标准库 |
| D28 | 编译期校验 | **CMake 期检查 + 静态断言，两工具链都要过** | 重复事件号/模块 ID/清单一致/层依赖方向 |
| D29 | 命名规范 | **统一前缀 + 事件号号段（`0xNN00`）** | `edge_`/`sys_`/`board_`/`app_` |
| D30 | 公共工具 | **`edge_util` 头文件库，归框架** | app 仍只依赖框架（不破 D1） |
| D31 | 事件号表 | **单一 `edge/events.h` + 编译期唯一性检查** | |
| D32 | 事件订阅 | **`main()` 显式 `sys_subscribe(id, app)`** | 与 D5 显式装配一致 |
| D33 | 事件时间戳 | **带单调时间戳（sink 打），不做序号** | |
| D34 | SDK 分发 | **要分发** → 契约版本化 + 打包 + 兼容矩阵，单独立项 | 分发会反过来要求 ABI 稳定 |
| D35 | 合规 | **有要求，单独立项**；当前只固化"认证边界"这一条架构约束 | 其余（证据/流程/文档）不进本方案 |
| D36 | 模块内部结构 | **`include/<name>/` 公共头 + `src/` 私有** | |
| D37 | 体积预算 | **CI 解析 map，设 flash/RAM 阈值** | 超阈即失败 |
| D38 | 可复现构建 | **固定工具链版本 + 记录 commit/工具链 hash** | |
| D39 | CI 矩阵 | **合法族×板组合全量构建 + 单测 + 两工具链** | |
| D40 | ABI/契约版本 | **只增不改、绝不复用**；版本号进 descriptor | 与 D34 分发绑定 |
| D41 | 日志/断言/追踪 | **注入 `log_port_t`**（同 D20 模式）；断言分级可编译期剔除 | 合规可能要求防篡改，待立项后补 |
| D42 | 持久化 schema | **版本号 + 迁移函数**（同 D40 思路） | |
| D43 | C++ 互操作 | **只保证 `extern "C"` 头可被 C++ include** | |
| D44 | 可移植性 | **框架层零架构相关代码**（IRQ/寄存器全在 board/infra） | |
| D45 | 认证边界 | **核心 = `edge_module`+`sys`+`board`+`infra`；app 在核心外** | 核心一次认证；app 可独立演进/分发 |
| D46 | 平台抽象层 | **PAL：框架只依赖极小原语** | 临界区/屏障/单调时间/上下文/ISR 桥接；由 board 实现 |
| D47 | 并发模型 | **单 runner 上下文** | 模块回调只在 runner 执行；裸机=main loop，RTOS=专用任务；`sys_step()` 可分解 |
| D48 | 驱动模型中立 | **infra 只依赖窄端口；驱动模型差异止于 soc / pal / glue** | 寄存器→`soc/<soc>`；OS/RTOS 模型→`pal/<os>`；绑定→product glue。`infra -> soc` **无例外**（由 `check_layer_dependencies.py` 强制） |
| D49 | SoC/板级中立 | **框架与 sys 零 SoC 知识** | SoC/引脚/时钟/复用/向量表全在 board；新 SoC = 新 board |
| D50 | RTOS 中立 | **框架不调任何 RTOS API** | RTOS 只出现在 product/main 与 PAL |
| D51 | 生命周期回调 | **`poll` + `on_event`（+可选 suspend/resume）** | init/deinit 由 main 显式调用；关停逆序 |
| D52 | 调度模型 | **事件驱动 + 周期轮询混合** | 模块声明 period；协作式预算告警；idle → board |
| D53 | 故障策略 | **init 失败默认跳过+记录；运行期默认隔离** | 看门狗在 board，sys 给健康信号 |
| D54 | 事件细节 | **丢最新+计数；`sys_publish` 仅 runner；可退订；可多订阅** | |
| D55 | 模块 ID | **中央 `edge/modules.h`，`0xNN00` 段** | 与事件号同构 |
| D56 | ABI 版本对象 | **`edge_module_t` 布局 + 事件号表 + 端口接口签名** | 编译期检查 |
| D57 | 测试策略 | **app 用 mock 端口 host 单测；board/infra 用 Renode 或 HIL** | 双工具链 + 矩阵 |
| D58 | 可测性约束 | **T-A..T-F 作为设计硬约束** | 无全局状态/依赖注入/时间注入/runner 可分解/零分配/PAL 可替换 |
| D59 | 单测框架 | **cmocka（host）** | 缺 cmocka 直接报错，不静默跳过 |
| D60 | 假实现约定 | **手写假 vtable，集中 `test/fakes/`** | 不用代码生成 |
| D61 | 假时钟/假 PAL | **host 用可控假时钟 + 空 PAL** | 支持确定性驱动 `sys_step` |
| D62 | Renode 范围 | **只测 board / infra** | 平台描述 + Robot 脚本 |
| D63 | HIL 范围 | **只测 Renode 建模不了的**（模拟前端/计量芯片/真实时序） | 最小集，不进日常 CI |
| D64 | CI 阶段 | **4 阶段：host 单测 → 目标构建矩阵 → Renode → 夜间 HIL** | 含中立性验收 |
| D65 | 多生产者队列 | **每生产者一个 SPSC 队列，归生产者所有，向 sys 登记** | 无锁、无临界区；sink = 生产者自己的队列 |
| D66 | 变长载荷 | **事件只带标量 + token；数据本体留 infra 缓冲区，app 用端口读** | 守住 D18 禁指针；避免一字节一事件 |
| D67 | 接口目录 | **新增第 20 节：框架 / sys / board+PAL / 端口 四组 API 清单** | 补上方案一直缺的一整层 |
| D68 | 错误码分配 | **中央 `edge/errors.h`，框架段 + 每模块段，编译期查重** | 基础值兼容 errno；与事件号同构 |
| D69 | runner 每步预算 | **有界：每队列最多 K 个事件、每模块最多一次到期 poll** | K 可配；避免事件风暴饿死 poll |
| D70 | 重入 | **`on_event` 内可 `sys_publish`，但入 runner 自有待处理队列延后** | 设最大深度，超限丢弃+计数 |
| D71 | 低功耗竞态 | **原子序列：关中断→复查→WFI/STOP→开中断** | 由 board/PAL 保证；RTOS 下交给 RTOS idle |
| D72 | 时间回绕 | **tick 比较一律用 `(int32_t)(now - due) >= 0`** | 禁止直接 `now >= due` |
| D73 | app↔module | **一个 app 可暴露 1..n 个 module（默认 1）** | 即"1 app ≥ 1 module" |
| D74 | 诊断聚合 | **`sys_stats_get()` 全局聚合；每模块计数可选** | 不引入额外基础设施 |
| D75 | ISR 有界 | **ISR 只允许清标志 + push 事件 (+pal_now)** | 静态检查/评审项 |
| D76 | bootloader/OTA | **本方案 out-of-scope，单独立项** | 只要求核心 ABI 稳定 + 版本号 |
| D77 | 资源所有权 | **硬件→board；设备→infra；内存→product；事件队列→生产者** | 四类资源各有唯一所有者 |
| D78 | app 间交互 | **仅两条：事件（事实）/ 服务接口（消费者定义 + 组合根适配）** | app 与 infra 在架构上平级 |
| D79 | 交互禁止项 | **禁直接 include 对方 / 全局变量 / 服务定位器** | |
| D80 | 无锁前提 | **单 runner 使 runner 内串行；ISR 不碰 app 资源** | 不需要独立仲裁器 |
| D81 | 独占/共享 | **独占资源一个所有者；共享设备由 infra 串行化** | 不在 app 之间协调 |
| D82 | 依赖顺序 | **组合根显式构造顺序；关停逆序** | 补 D5/D51 |
| D83 | 缺失依赖 | **组合根 glue 提供替代实现（产品变体）** | 编译期发现 |
| D84 | 共享中断 | **board_irq_attach 支持同一 IRQ 多 handler，按注册序分发** | 每个 handler 有自己队列（D65） |
| D85 | PAL 分层 | **`pal/` 独立成层（架构 × RTOS）；board 只选择** | 修正 D46：同一板换 RTOS 不重复实现 PAL |
| D86 | product↔board 绑定 | **product 显式绑定唯一 board，且不可被构建参数覆盖** | 绑定在 `edge_add_product()` 时记录；product 名只可注册一次；`board -> exactly one soc`；中立性夹具 `edge_add_minimal_variant` 是测试夹具、豁免本约束 |
| D87 | RTOS 配置归属 | **FreeRTOS 配置由 product 组合（soc/board/product 三层）；PAL 只声明契约与必需不变量** | PAL 以 `#error` 强制栈溢出检测、高水位 API 与非静默 assert；同一 kernel 只服务一份配置，多 kernel 待第二个 RTOS 产品出现再做 |

### D5 反转后：装配显式，顺序仍由 sys module 决定

放弃自动注册后，"有哪些 app"由 `main()` 显式列出：

```c
/* product/<name>/main.c */
dlt645_t *a = dlt645_new(&dlt645_storage_adapter);
dlms_t   *b = dlms_new(&dlms_storage_adapter, &rtc_adapter);
relay_t  *c = relay_new(&relay_gpio_adapter);
edge_module_t *apps[] = { &a->mod, &b->mod, &c->mod };   /* 显式列表 */
sys_run(sys_meter_family, apps, 3);
```

- **发现** = main() 的显式列表（不再有隐式注册）。
- **编排** = 族 sys module 按 priority 排序后依次调用（D11）。
`!` 这样"产品装了啥"一眼可见，裁剪 = 列表里不写它。

### D6：构造注入（T2 已解决）

D6 最初在 A/B/C 之间摇摆，最终由 D14（Go 原则）拍板为 **A：构造注入**：

```c
dlt645_t *app = dlt645_new(&dlt645_storage_adapter);   /* 依赖写在签名里 */
```

依赖可见、可替换（测试给假适配器）、无服务定位器、无全局服务 ID。
`!` 曾担心 A 与 D5 自动注册冲突 —— D5 已反转为显式装配，冲突消失。

### D3 的后果：设备访问去哪儿了

board 只转发中断 ⇒ 外设读写（UART/ADC/继电器）**不能**由 board 提供。那它只能来自两类之一：
- 作为"基础设施"被组合根选型后注入（driver/infra port）；
- 或 app 自己直接摸寄存器（`!` 这会让 D1/D3 全部破产）。

结论：**"设备层"是组合根选型的一部分，不是 board 的一部分**。当前 SDK 的 `edge_device_t` 应归属 infra，不归 board。

### D9："故障/低功耗归 board"太笼统 —— 用两个例子说明（Q9b）

"归 board"没说你到底把**哪些决定**交给 board。同一个故障/休眠，其实有 3 类完全不同的决定：

| 类别 | 例子（休眠） | 例子（app 卡死） | 谁有能力做 |
|---|---|---|---|
| 硬件动作 | 关外设时钟、MCU 进 STOP | 看门狗超时→硬件复位 | 只有 board 能（寄存器级） |
| 软件策略 | 现在能不能睡（app 都空闲？）| 隔离这个 app、跑别的、要不要重启它 | 只有 sys module 知道 app 列表 |
| 模块状态 | 睡前把计量数据落盘 | 上报"我不行了" | 只有 app 知道自己要存什么 |

**已答（Q9b）：不放。** board 只做第 1 类（硬件动作）；第 2 类（软件策略）归 sys module；第 3 类（模块状态）归 app。
`!` 这条要落到接口上：board 不暴露任何"app 列表/模块状态"API，否则等于变相越界。

### 产品裁剪（D5 反转后）

不再依赖链接器裁剪：**在 main() 的列表里不写它，就不存在**。裁剪变成显式的、可读的。构建系统仍负责"哪些源码参与编译/链接"，但"装了哪些模块"由 main() 列表表达。
`!` CMake target 与 main() 列表要保持一致；建议两者放在同一个 `product/<name>/` 里，评审时一起看。

### D7：不要 manifest —— 产品的唯一事实来源是"构建系统 + main()"

D7 不上 manifest。产品由两个地方共同定义，且必须放在同一目录一起看：

1. **构建系统**：编译/链接哪些源码；
2. **`main()` 的显式列表**：装配哪些 app、注入哪些适配器。

```cmake
# product/<name>/CMakeLists.txt
add_executable(product_meter_a_overseas
    main.c glue/adapters.c)
target_link_libraries(product_meter_a_overseas PRIVATE
    sys_meter_family
    board_rn8615
    app_dlt645 app_dlms app_relay app_lcd)
```

```c
/* product/<name>/main.c —— 与上面链接列表一一对应 */
dlt645_t *a = dlt645_new(&dlt645_storage_adapter);
...
edge_module_t *apps[] = { &a->mod, &b->mod, &c->mod };
sys_run(sys_meter_family, apps, 3);
```

`!` 两处清单必须一致，否则会出现"链接了但没装配"或"装配了但没链接"，建议加一个编译期/启动期校验。

### D11：排序用"优先级表"，不是显式 ID 列表

族的 sys module 不靠链接顺序，而是按每个 app 的 `priority` 排序后依次调用：
- app 在 descriptor 里带 `priority`（SDK 已有该字段，目前从未被使用）；
- 同优先级必须定 tie-break（建议按 `module_id` 升序），否则顺序仍不确定；
- 优先级只决定**顺序**，不决定"必需/可选"。仍需要一份**必需模块集合**做启动校验，否则 main() 里漏写一个关键 app 不会报错。
`!` 优先级表解决顺序，解决不了"缺模块检测"——两者分开做。

### D12/D14：Go 原则 —— 使用者定义接口，提供者只给具体 API

采纳 Go 的"使用者定义接口，而不是提供者"。C 语言下的具体形状：

```c
/* 1. 使用者（app）自己定义它需要什么 —— app/dlt645/dlt645.h */
typedef struct {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
} dlt645_storage_if;

dlt645_t *dlt645_new(dlt645_storage_if *storage);   /* 显式注入，Go 风格 */

/* 2. 提供者（infra）只提供自己的具体 API —— infra/flash/flash.h */
edge_status_t flash_read(void *self, uint32_t off, void *buf, size_t len);

/* 3. 适配器写在组合根 —— product/<name>/glue/adapters.c */
static edge_status_t ad_read(void *self, uint32_t key, void *buf, size_t len) { ... }
static dlt645_storage_if g_dlt645_storage = { .read = ad_read, .write = ad_write };

/* 4. 装配在 main() —— product/<name>/main.c */
dlt645_t *app = dlt645_new(&g_dlt645_storage);
```

三方职责：
- **接口定义** → app（使用者）
- **具体实现** → infra
- **适配器 + 装配** → 组合根（glue + main）

**这条正好解开了前面卡住的循环**：接口**跟着 app 走**，不跟着 glue 走。所以：
- 通用 app 仍然跨产品：它在 `app/` 里定义自己的接口，编译一次；
- 每个产品在自己 `glue/` 里写适配器，把本产品的 infra 供上去；
- **无需独立 `contracts/`，也无需"统一服务表/服务 ID"**。
- 产品私有端口自然成立：app 若定义了一个只有本产品能提供的接口，它就只能在那个产品里装，不需要额外机制。

### D45：认证边界 —— 与静态装配（D5）的关系（重要）

结合 D34（要分发）+ D35（有合规），认证边界定为：**可信核心 = `edge_module` + `sys` + `board` + `infra`；app 全部在核心外。**

`!` 但必须澄清一个容易搞错的点：**"app 在 TOE 外"不等于"运行期隔离"。** 因为 D5 是**静态装配**（app 链接进固件），app 与核心在同一个二进制里。所以：

- 边界由 **源码/变更控制边界 + 核心 ABI 版本 + 固件签名** 保证，不是运行期沙箱。
- 核心作为**可复用的已认证组件**；各产品/团队在自己固件里组合它，核心认证可复用（组件认证/组合）。
- 因此 `SEC-8`（eBPF/WASM 沙箱）**不**因 D45 变成必选 —— 它只在"运行期可加载第三方模块"的模型下才需要，而那与 D5 冲突，属于另一个未来工作流。
- 真正被 D45 升级为硬要求的是：**D40（核心 ABI 稳定 + 版本化）** 和 **D34（分发兼容矩阵）**。

`!` 如果未来真要做"运行期第三方模块"，那将是 D5 的一次局部推翻（部分模块改为可加载），应单独立项。

### D46–D50：平台抽象与中立性（多 SoC / 多外设 / 多驱动模型 / 多 RTOS）

要让同一套 app/sys/infra 代码跑在"各种 SoC、各种外设、各种驱动模型、各种 RTOS"上，必须补两块地基：

**1. PAL（Platform Abstraction Layer，D46）**
框架只依赖极小的平台原语，由 board 实现：
- `pal_critical_enter/exit()` —— 临界区
- `pal_mb()` —— 内存屏障
- `pal_now()` —— 单调时间
- `pal_in_isr()` —— 当前上下文
- `pal_isr_enter/exit()` —— RTOS 感知的中断进入/退出（如 FreeRTOS 的 `portYIELD_FROM_ISR`）

裸机与 RTOS 的差异全部止于 PAL。

**2. 并发模型：单 runner 上下文（D47）**
- 所有模块回调（`poll`/`on_event`/`suspend`/`resume`）只在**一个 runner 上下文**执行。
- 裸机：runner = `main` 里的 `while(1) sys_step();`
- RTOS：runner = product 创建的**一个专用任务**，调 `sys_step()`；其它任务/ISR 只能把事件投给队列。
- 框架**不创建任务、不调 RTOS API**。所以 `sys_run()` 与 `sys_step()` 分开，两种模型共用。
`!` 这样"前后台"被泛化为"runner 上下文 / 其它上下文"，RTOS 与裸机不需要两套 app。

**3. 中立规则（D48/D49/D50）**
- **SoC/板级**：SoC、引脚、时钟、复用、向量表**只在 board**；新增 SoC = 新增 board（可能新增 PAL 实现）。
- **驱动模型**：infra 可建在寄存器、厂商 HAL、Zephyr、Linux 之上；对 app 只暴露适配后的窄接口；差异止于 infra + product/glue。
- **RTOS**：只有 product/main（建任务、建队列）和 PAL（ISR 桥接、临界区）知道 RTOS 存在。

**4. board↔infra 边界（原待讨论第 5 项，现定）**
- board：SoC/板级 init、时钟/引脚/复用、**向量表所有权**、PAL 实现、硬件动作
- infra：外设逻辑；通过 `board_irq_attach(irq, cb, ctx)` 登记；board 的 ISR 调 `cb`，`cb` 只做"清标志 + 投递事件到 sink"
- 即：**board 管引脚/时钟/中断，infra 管外设功能**。

---

## 0. 先用一句话把你的架构说清楚

你描述的是一个 **三轴软件产品线（SPL）**，不是一个普通的模块框架：

```
                 轴1: 产品族            轴2: 板子
              ┌──────────────┐    ┌──────────────┐
              │  sys module   │    │ board module │
              │  (每族一个)    │    │ (每板一个)    │
              └───────┬───────┘    └───────┬──────┘
                      │                    │
        ┌─────────────┴────────────────────┴─────┐
        │           组合根 composition root        │  ← main()
        │  选型 + 构造 + 注入 + 决定装入哪些 app      │
        └─────────────────┬──────────────────────┘
                          │
                 ┌────────┴────────┐
                 │  app modules 仓库 │  ← 所有产品共享、零依赖
                 └─────────────────┘
```

- **轴1 产品族**（行为/策略差异）→ sys module
- **轴2 板子**（硬件差异）→ board module
- **核心资产**（可复用业务）→ app modules
- **变体装配**（选哪个族 + 哪块板 + 哪些 app + 哪些 infra）→ 组合根

`!` 这个架构能不能长期守住，取决于一件事：**"变体"和"核心"的边界是否真的干净**。下面每一节都在戳这条边界。

---

## 1. 三层职责边界（谁被允许知道谁）

你的原话里隐含了"应用模块零依赖"，但没说零依赖是**对谁**。这是整个设计的命门。

| 层 | 允许知道 | 禁止知道 |
|---|---|---|
| app module | 自己定义的接口、框架生命周期头、标准 C | 其它 app、具体 infra、HAL/寄存器、产品族、板子 |
| sys module | 产品族策略、app 列表、被注入的 infra 句柄 | 具体寄存器、具体板子型号 |
| board module | MCU/板级寄存器、引脚、时钟、IRQ | 业务、产品族、app 的私有类型 |
| infra（存储等）| 自己的驱动、HAL | app 的业务类型 |
| 组合根 | **全部具体类型** | 业务逻辑（只做装配） |

**已定（D14 / Go 原则）：ALT-A。** 接口由 app（使用者）自己定义，infra 只给具体 API，适配器写在产品 glue。
`!` 事件号仍需中央分配表（D4），但不能反过来让 app 依赖 board；board 只发事件、不定义 app 的行为。

---

## 2. "零依赖"到底要禁到什么程度

把"零依赖"拆成 5 个可判定的条款，才能靠 CI 强制：

- [ ] N1 不直接 include 任何其它 app module 的头
- [ ] N2 不直接 include 任何具体基础设施的头（只 include 契约）
- [ ] N3 不直接访问寄存器/外设地址（`#include "stm32xxx.h"` 一律禁）
- [ ] N4 不依赖 RTOS / 线程 / 动态分配（前后台模型）
- [ ] N5 不依赖 libc 的重资源部分（printf/malloc）—— 或明确允许

`?` "零依赖"是否也包含**框架头**？app 总得知道 `edge_module_*` 和"我有个 loop()"，那它至少依赖 SDK。所以更准确的说法是：**"只依赖契约层，不依赖任何具体实现"**，而不是"零依赖"。
`?` 禁止手段用哪种：`#include` 白名单脚本 / 链接期符号隔离 / 单独编译一个 app 目标时只给契约 include 路径 / 代码评审。我倾向"编译目标只暴露契约 include 路径"——最便宜且可 CI。

---

## 3. sys module 深挖：它到底是"一个实现"还是"一张配置表"

你的原话："sys module loop() 调用所有的应用模块，每个产品族一个"。

`?` 这里藏着一个大分叉：

- ALT-A **每族一份 sys.c**：变体差异直接写代码分支。简单，但 N 个族 × 相同循环骨架 = 复制粘贴。
- ALT-B **一个通用 sys module + 每族一张表**：表里声明"本族启用哪些 app、什么顺序、什么周期"。变体变成**数据**。更符合产品线，但表要先设计出来。
- ALT-C **契约/接口 + 每族一个实现**（sys module 是接口，族是实现）→ 灵活但抽象重。

`!` 我倾向 ALT-B：**loop 骨架只写一次，产品族差异全部下沉为配置**。否则"每个产品族一个"会退化成"每个产品族抄一遍"。

再往下的 loop 契约必须钉死：

- [ ] L1 loop 的调用对象是谁：只调"已注册且 autostart"的 app？
- [ ] L2 顺序如何确定：注册顺序 / 优先级 / 拓扑（依赖 DAG）？
- [ ] L3 周期：固定 tick？每轮全调？分频（快任务每轮、慢任务每 N 轮）？
- [ ] L4 时间预算：单个 app 的 loop 有无上限？超时怎么办（告警/隔离/看门狗）？
- [ ] L5 loop 里能不能阻塞、能不能分配、能不能调用 infra 的长操作？
- [ ] L6 相位：是否强制"采集→协议→业务→输出"这种固定相位，而不是"app 顺序"？

`?` **sys module 与组合根职责重叠吗？** 我认为要切开：
- 组合根 = **装配期**：决定"用哪个存储实现、装入哪些 app"。
- sys module = **运行期**：决定"什么时候、按什么顺序、跑哪些 app"。
- `!` 如果 sys module 也开始 new 具体对象，就变成了第二个组合根，架构立刻腐烂。

---

## 4. board module 深挖：中断转发不是"转一下"那么简单

你的原话："board module 硬件中断转发，每个板子一个"。

先定义接口。它至少要有：

```
ISR (board module 独占)  →  [最小化采集]  →  event queue  →  sys loop → app
```

必须发散的点：

- [ ] B1 IRQ→event 的**映射表**从哪来：手写数组 / device tree / 配置生成？板子换一个，表就换一张。
- [ ] B2 board module 是否只做"中断转发"，还是也提供**设备访问**（UART 读写、ADC 采样）？若只转发，app 拿什么读外设？
- [ ] B3 中断里允许做什么：清标志、抓最小数据、入队——**绝不允许调 app 回调**。这条要写死并静态检查。
- [ ] B4 背压：队列满了丢哪个（最旧/最新）、丢事件算不算故障、要不要计数上报？
- [ ] B5 共享中断：多个源共用一个 IRQ 时怎么分发（离线包里有"共享中断"资料，值得翻）？
- [ ] B6 板级差异的粒度：引脚号？时钟树？外设实例（UART1 vs UART3）？外设是否可能"半共享"（总线仲裁）？

`!` 陷阱：board module 很容易长成"什么都往里塞的 BSP 上帝"。它应该**只做两件事**：拥有中断向量、拥有硬件资源实例；业务一律不进来。

`?` **谁定义事件号？** 见第 0 节的 ALT-C——这是 board 和 app 之间最扎手的地方。board 认识事件号、app 也认识事件号，那事件号必须住在中立契约包里。

---

## 5. 组合根深挖：你只有一次机会把依赖关系写对

你的原话："选择其它基础设施，比如存储子系统；这些依赖作为组合根传递给 sys_module；main()"。

`?` **为什么必须"单一"组合根？** 因为一旦 infra 的构造散落在各处，依赖关系就不可视、不可测。组合根是唯一允许 `#include` 所有具体实现的地方。

要发散的：

- [ ] C1 基础设施候选清单（先列全，再谈接口）：存储 key/value、文件系统、日志/trace、时间/RTC、通信（UART/485/CAN/以太）、配置、OTA、看门狗、加解密。**哪些是"每个产品族都必须有"、哪些是"可选"？**
- [ ] C2 注入形态：`struct services { const storage_port_t *storage; ... }` 一次性传进去，还是逐个构造注入？我倾向**一个 services 结构体**，sys module 持有并再分发给 app。
- [ ] C3 分发给 app 的方式：组合根直接注入每个 app？还是 sys module 提供 `get_service()`？`!` 后者会沦为**服务定位器反模式**（隐藏依赖、难以测试）。倾向：**组合根构造 app 时显式传入它需要的那几个 port**。
- [ ] C4 每个 app 声明"我需要哪些 port"从哪来：手写构造调用 / manifest 声明后生成？
- [ ] C5 组合根怎么避免变成上帝：只允许"构造 + 连接 + 启动"，禁止 if/else 业务分支。

`?` **main() 的形状**应该是几行？我期望是：

```
main() {
    board_init();                 // 硬件与中断
    infra = build_infrastructure();// 选型
    apps  = build_apps(&infra);    // 装配, 显式列出
    sys   = sys_create(apps);      // 产品族策略
    sys_run(sys);                  // 永不返回的 loop
}
```

如果 main 长到几十行 if/else 选型，说明"选型"这件事没被抽象成**构建变体**（见第 7 节）。

---

## 6. 事件与调用：三层之间到底用哪种耦合

app 零依赖，那 app 之间、app 与 board 之间只能靠**事件**或**契约**。这里要定死规则：

- [ ] E1 app↔app：只允许事件，还是也允许 contract 直接调用？
- [ ] E2 事件寻址：广播 vs 按订阅分发 vs 按模块 id 定向？
- [ ] E3 队列归属：每个 app 一个队列 / 全局一个队列 + router / 每类事件一个队列？
- [ ] E4 事件里能不能带指针？带了谁保证生命周期（ISR 栈对象绝对禁止）？
- [ ] E5 事件是"事实"（已发生）还是"命令"（请去做）？——语义混用是后期最大的坑。
- [ ] E6 需不需要时间戳/序号，以支持重放与追踪？

`!` 直觉：**ISR 产生的走事件，app 主动发起的走契约调用**。命令/事件分开建模。

---

## 7. N×M 产品矩阵：这是产品线的真正难点

你有 N 个产品族 × M 个板子。发散：

- [ ] P1 是不是任意组合都合法？还是只有白名单组合？（如族 A 只能配板 1/2）
- [ ] P2 非法组合谁来拦：CMake / manifest / 编译期断言？
- [ ] P3 一个 app module 如何被多个族复用而不被"污染"（加族特判）？
- [ ] P4 构建系统怎么表达"族×板×app集合"：CMake preset / target 组合 / manifest 生成。
- [ ] P5 谁负责"这个族需要哪些 port"的完整性校验（缺 port 就链接失败）？

`?` 是否引入一份 **manifest（每产品一份）**：`family、board、apps[]、requires_ports[]、provides[]`。它可以同时喂给构建、喂给组合根代码生成、喂给 CI 校验矩阵。这可能是把"组合根手写"变成"组合根生成"的关键一步。

---

## 8. 仓库与打包：你写的"所有产品的所有应用模块仓库"

- [ ] R1 单仓（monorepo）还是每模块一仓？你写的是"仓库"（单数），倾向 monorepo。
- [ ] R2 目录约定：`modules/<name>/` + 独立 `CMakeLists.txt` + `include/<name>/port.h`。
- [x] R3 契约放哪：**已定**，接口跟 app 走（D14），不设独立 contracts 层。
- [ ] R4 每个 app 声明零依赖的**元数据**（manifest）放哪。
- [ ] R5 契约版本化与兼容策略（app 按契约版本编译）。
- [ ] R6 谁拥有"事件号/契约 ID"的分配（防撞号）——集中分配表 or 按模块前缀分段。

`!` 事件号/契约 ID 的**号段划分**要尽早定，否则日后两个模块撞号，运行期才炸。

---

## 9. 启动顺序与注册：已选显式（ALT-B）

- ALT-A 链接段自动注册：否决（D5 反转）。
- **ALT-B 组合根显式列表：已选**。产品需要哪些 app，在 `main()` 里明确列出并构造。
- ALT-C manifest 生成列表：否决（D7 不上 manifest）。

启动顺序（已定方向）：`board → infra → 适配器 → app_new(deps) → sys_run`。
`!` app 的接口依赖由 `app_new(deps)` 在构造时一次性给足，所以 infra 与适配器必须在 app 构造之前就绪。

---

## 10. 错误 / 故障 / 低功耗：三层各自的策略

- [ ] F1 app 初始化失败：跳过它 / 整机失败 / 降级运行？谁决定（sys module 还是组合根）？
- [ ] F2 board module 收到未知中断：忽略 / 计数 / 断言？
- [ ] F3 一个 app 反复异常：隔离（停止调度它）还是重启它？状态在哪持久化？
- [ ] F4 低功耗：suspend/resume 谁发起？board 关外设、infra 落盘、app 存状态，顺序是什么？
- [ ] F5 看门狗：喂狗在 sys loop 还是 board？某个 app 卡死如何被检测（执行预算）？
- [ ] F6 死机现场：谁保存、存到哪（infra storage）、下次启动谁读。

`!` 这些策略**必须分层**：app 只管"我这个模块的状态"，sys 管"整机调度与降级"，board 管"硬件级安全动作"。混在一起就是灾难。

---

## 11. 测试矩阵（分层可测是这套架构最大的红利）

- [ ] T1 app module：单测 + mock port，**不需要任何硬件**。这是"零依赖"换来的核心价值，必须兑现。
- [ ] T2 sys module：给假 app 列表，测调度顺序/预算/降级。
- [ ] T3 board module：Renode 仿真或硬件在环，测 IRQ→event。
- [ ] T4 组合根：集成测试，验证"选型正确 + 依赖齐全"。
- [ ] T5 产品矩阵：CI 对合法组合全量构建，确保没有"只有某个组合才编不过"。
- [ ] T6 契约演进：契约加字段后，所有 app 仍能编译/行为不变。

`?` 如果 T1 做不到（app 一编译就得拉一堆东西），那"零依赖"就是口号没落地。

---

## 12. 反模式清单（提前写下来当评审检查项）

- `!` 组合根出现业务 if/else（选型逻辑泄漏）。
- `!` app 偷偷 `#include` 具体 infra 头（编译目标只给契约 include 路径可防）。
- `!` sys module 膨胀成业务逻辑 + 变体判定的混合体。
- `!` board module 泄漏寄存器定义给 app。
- `!` 用服务定位器 `get_service()` 隐藏依赖。
- `!` 事件当命令用 / 命令当事件用。
- `!` 事件号散落各处、无中央分配表。
- `!` app 之间直接链接（破坏零依赖与可裁剪）。
- `!` 组合根构造顺序隐式依赖全局初始化顺序。

---

## 13. 决策状态：已全部冻结

D1–D24 全部拍板（见文首《决策记录》）。第 1 层横切已收敛，进入第 2 层（工程机制）。

**必须落到设计里的纪律（不算未决）：**
- 构造注入纪律：app 只通过 `app_new(deps)` 接收依赖，绝不从全局/服务定位器取。
- 接口归属：接口由 app 定义；infra 只给具体 API；适配器写在产品 glue。
- tie-break：同优先级 app 的顺序规则（建议 `module_id` 升序）。
- 必需模块集合：优先级表不表达"缺模块"，需单独一份启动校验清单。
- 一份清单：`product/<name>/CMakeLists.txt` 与 `main.c` 的 app 列表必须一致。
- board 边界：不暴露任何"app 列表/模块状态"API。

**目录约定（monorepo）：**
```
edge_module/          框架契约（descriptor/生命周期/事件队列）
app/<name>/           通用 app：自带它需要的接口 + app_new(deps)
sys/<family>/         产品族 sys module（priority 排序 + 策略）
board/<board>/        板级 board module（只转发中断 + 硬件动作）
infra/<name>/         基础设施实现（只提供自己的具体 API）
product/<name>/
    glue/             适配器：把本产品 infra 适配成各 app 定义的接口
    main.c            显式装配（app_new + 填 deps）
    apps/             产品私有 app
```
组合根 = `product/<name>/`（glue + main）。因为接口在 app 侧，所以各产品各自适配即可，通用 app 仍可跨产品。

### D14 的连锁反应：已按"出路 1"解决

消费者定义接口 ⇒ 组合根必须显式 `app_new(类型化依赖)` ⇒ 装配点必须知道每个 app。因此：

- **D5 反转**：放弃链接段自动注册，改为 `main()` 显式装配。
- **D6 回退到 A**：构造注入取代"声明 + 统一服务表"。
- 连带：不需要全局服务 ID、不需要 `--whole-archive` 讲究、产品裁剪变成"列表里不写"。

---

## 14. 与 edge-module-sdk-v2 的对应关系

- `edge_module_manager` → 未来族 sys module 的骨架；但入口从"遍历链接段"改成"遍历 main 传入的显式列表"。
- `edge_module_descriptor/preamble` → app/board 的**生命周期契约**，保留；但 `descriptor.config/resources` 不再用于"声明式端口表"（D6 已改 A）。
- `edge_event_queue` → board→app 的事件通道，保留。
- `edge_module_init_all()`（依赖 `__start/__stop`）→ **按 D5 反转应废弃**，改为 `sys_run(family, apps[], n)` 接管显式列表。
**结论**：SDK 的链接段自动注册与本架构（D5 显式装配）方向相反，应删除或降级为可选。
（SDK 当前的编译/越界/泄漏等具体缺陷另见调研结论。）

---

## 15. 可执行的下一步（发散收敛）

- [x] S1 决策已全部冻结（D1–D24，见《决策记录》）。
- [ ] S2 画出三层"知识边界表"的最终版（第 1 节那张表，逐格确认）。
- [ ] S3 跑通最小闭环：app 定义 `dlt645_storage_if` + `dlt645_new(deps)`；infra 给 flash API；product glue 写适配器；main 装配。
- [ ] S4 定义 board→event 的最小映射表与背压规则，用一个真实 IRQ 打样。
- [ ] S5 搭 monorepo 目录骨架（edge_module/app/sys/board/infra/product）与一个产品 target。
- [ ] S6 写"接口隔离"的 CI 检查：app 的 include 路径不含任何 infra/product 目录。
- [ ] S7 反例验证：故意让 app include infra 头，确认编译/CI 能拦住。
- [ ] S8 定 tie-break（module_id 升序）、"必需模块集合"启动校验、以及 CMake 列表与 main() 列表的一致性校验。

---

## 16. 四种能力的缺口清单（只写方案，待决策）

> D1–D14 解决的是"骨架"。本节解决"骨架之外的横切与工程能力"。
> 状态：`[ ]` 待决策 / `[~]` 部分 / `[x]` 已有方案。

| 能力 | 覆盖度 | 已有 | 缺口重点 |
|---|---|---|---|
| 组织代码 | ~70% | 目录、三层边界、接口归属 | 规范与强制机制 |
| 构建系统 | ~40% | 每产品一 target、无 manifest | 双工具链、矩阵、编译期校验 |
| 前瞻设计 | ~30% | 已列条目（多核/可加载/OTA…） | 横切关注点（错误/时间/内存） |
| 抽象 | ~55% | 端口/适配器/组合根 | 事件/错误/时间/存储抽象 |

### 16.1 组织代码（ORG）

已有：monorepo 目录约定、三层职责边界表、零依赖规则、接口归 app / 适配器归 glue。

- [x] ORG-1 命名规范：统一前缀 + 事件号号段 → D29
- [x] ORG-2 每层"可 include 什么"的规则，强制机制 → D27
- [x] ORG-3 模块内部结构：`include/<name>/` 公共头 + `src/` 私有 → D36
- [x] ORG-4 **公共工具代码放哪**：`edge_util` 归框架 → D30
- [x] ORG-5 事件号分配表的物理位置与格式：`edge/events.h` → D31
- [~] ORG-6 代码所有权：组织问题，不进技术方案

### 16.2 构建系统（BLD）

已有：每产品一个 CMake target + 显式链接列表、无 manifest、两处清单一致性校验。

- [x] BLD-1 **双工具链**：GCC + IAR → D25（CMake 驱动 iccarm）
- [x] BLD-2 N×M 矩阵在 CMake 里的表达方式 → D26
- [x] BLD-3 include 路径隔离的机制 → D27
- [x] BLD-4 编译期校验：重复事件号、重复模块 ID、清单一致、层依赖方向 → D28
- [x] BLD-5 体积预算（flash/RAM per product）+ map 校验 → D37
- [x] BLD-6 可复现构建 / 工具链文件 / cross-compile → D38
- [x] BLD-7 SDK 分发与版本化 → D34（要分发）
- [x] BLD-8 CI 矩阵：合法族×板组合全量构建 → D39

### 16.3 前瞻设计（FWD）

已有条目（不重复）：多核留接口（D10）、可加载模块/安全（SEC）、OTA/回滚、多实例、诊断、调度。

- [x] FWD-1 **错误模型跨层统一**（`edge_status_t` 在 app/sys/board/infra 各层的语义）→ D19
- [x] FWD-2 **时间/时钟抽象**（tick / RTC / 单调时钟）→ D20
- [x] FWD-3 **内存模型**（静态分配、栈预算、池、生命周期）→ D21
- [x] FWD-4 **事件模型跨层定稿**（第 6 节 E1–E6）→ 已定 D16–D18、D33、D54
- [x] FWD-5 日志 / 断言 / 追踪接口 → D41
- [x] FWD-6 持久化 schema 演进（表计参数与数据）→ D42
- [x] FWD-7 安全与合规（EAL3 / CRA / 计量法规）→ D35（单独立项）
- [x] FWD-8 C++ / 跨语言互操作 → D43
- [x] FWD-9 可移植性（非 ARM） → D44

### 16.4 抽象（ABS）

- [x] ABS-1 事件抽象（事实 vs 命令）→ D16
- [x] ABS-2 错误抽象 → D19
- [x] ABS-3 时间抽象 → D20
- [x] ABS-4 存储抽象形状（KV / 块设备 / 字节流？）→ D23
- [x] ABS-5 设备抽象形状（board 不做设备后，infra 的设备接口长什么样）→ D24
- [x] ABS-6 接口粒度（一个大 `deps` 结构 vs 多个小接口）→ D22
- [x] ABS-7 契约 / ABI 版本演进 → D40
- [x] ABS-8 **框架自身边界**：`edge_module` 只剩"生命周期 + 事件"→ D15

### 16.5 元问题

- [x] META-1 `edge_module` 新形态已定（生命周期结构 + 事件队列，D15）；待落：同步修正 SDK 的 `edge_module_manager` / `edge_module_init_all`

### 16.6 建议优先级（不补这些，落地时必露洞）

1. `FWD-4` 事件模型 —— 它是 board↔app↔sys 三层的唯一耦合面。
2. `FWD-1` 错误模型 —— 每层都要用，晚定就是全局返工。
3. `FWD-2` 时间抽象 —— 调度、超时、计量都依赖它。
4. `BLD-1`/`BLD-2` 双工具链 + 矩阵。
5. `FWD-3` 内存模型 —— 嵌入式不能回避。
6. `META-1`/`ABS-8` 框架自身边界 —— 决定 `edge_module` 留多少。

### 16.7 IAR（D25）的连带影响

D5 反转（放弃自动注册）把 IAR 支持里最难的部分直接删掉了：

- **不再需要 `.edge_module_registry` 链接段**，也就不需要 IAR 的 `#pragma location` / `__section_begin/end` 段边界机制 —— `docs/iar.md` 里一半内容作废。
- 剩下的 IAR 要求变得很低：
  1. **C 方言可移植**：框架/接口不用 GNU-only 扩展；`_Static_assert` 走 C11 或降级宏。
  2. **编译期校验要两工具链都能过**（D28）：优先 `_Static_assert` + CMake 期检查，不用 GNU `__attribute__((error))`。
  3. **include 路径一致**：D27 的隔离在 CMake 里定义，`.ewp` 只做调试，不得自成一套路径。
  4. `docs/iar.md` 需重写：删段放置，保留编译选项（`--c11`）与 .ewp 生成/同步流程。

---

# 17. 架构方案（收敛稿 · 详细）

> 本节是 D1–D45 的收敛结果，供评审与实施。发散过程与依据见上文第 0–16 节。

## 17.1 目标

一套**三轴软件产品线架构**：产品族（sys）× 板子（board）× 应用模块（app），由**组合根（product）**装配。
目标：app 零具体依赖、可跨产品复用、可分发；核心可认证；启动与装配显式可读。

## 17.2 总体结构

```
                轴1 产品族            轴2 板子
              sys/<family>          board/<board>
                      \               /
                       product/<name>         ← 组合根（变体装配）
                      /               \
             app/<name> (核心资产)    infra/<name>
                      \               /
                       edge_module            ← 极薄框架
```

- 轴1 产品族 → sys module（每族一份实现）
- 轴2 板子 → board module（每板一份）
- 核心资产 → app（跨产品共享）
- 变体装配 → product（组合根）

## 17.3 分层职责与知识边界

| 层 | 允许知道 | 禁止知道 |
|---|---|---|
| app | 自己定义的接口、`edge_module`、标准 C | 其它 app、具体 infra、寄存器、产品族、板子 |
| sys | 产品族策略、app 列表、注入的端口 | 具体寄存器、板子型号 |
| board | MCU/板级寄存器、引脚、时钟、IRQ | 业务、产品族、app 私有类型 |
| infra | 自己的驱动、HAL | app 业务类型 |
| product（组合根）| 全部具体类型 | 业务逻辑（只装配） |

### 零依赖条款（可判定，靠 CI 强制）
- N1 不 include 任何其它 app 的头
- N2 不 include 任何具体基础设施的头
- N3 不访问寄存器/外设地址
- N4 不依赖 RTOS / 线程 / 动态分配
- N5 不依赖 libc 重资源（printf/malloc）或明确白名单

## 17.4 目录与依赖方向

> 下面是基础版；纳入多 SoC / 多外设 / 多驱动模型 / 多 RTOS 后的**完整目录见第 24 节**。

```
edge_module/
    include/edge/module.h        生命周期契约
    include/edge/events.h        事件号分配表（唯一）
    include/edge/util/*.h        ringbuf / crc / bit（头文件库）
    src/module.c event.c
app/<name>/
    include/<name>/*.h           自己需要的接口 + 工厂/init 声明
    src/*.c                      实现 + 私有头
sys/<family>/                    include/ + src/
board/<board>/                   include/ + src/
infra/<name>/                    include/ + src/
product/<name>/
    glue/*.c                     适配器（infra → app 接口）
    main.c                       显式装配
    apps/                        产品私有 app
```

依赖方向（无 contracts 层）：
```
app     → edge_module  + 自己定义的接口
sys     → edge_module
board   → edge_module
infra   → edge_module
product → 全部
```
接口跟着 app 走；适配器归 product/glue；board 永不认识 app。

## 17.5 装配与启动

```c
/* app/dlt645/dlt645.h —— 使用者定义接口 */
typedef struct {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
} dlt645_storage_if;

/* 调用者提供存储，运行期零分配 */
int dlt645_init(dlt645_t *self, const dlt645_storage_if *storage);

/* product/<name>/glue/dlt645_glue.c —— 适配器 */
static edge_status_t ad_read(void *s, uint32_t k, void *b, size_t n)
{ return flash_read(s, k, b, n); }
static const dlt645_storage_if g_dlt645_if = { ad_read, ad_write };

/* product/<name>/main.c —— 显式装配 */
board_init();
flash_t flash; flash_init(&flash);
dlt645_t dlt645_mem; dlt645_init(&dlt645_mem, &g_dlt645_if);

edge_module_t *apps[] = { &dlt645_mem.mod, ... };
sys_subscribe(&sys, EDGE_EVT_UART0_RX, &dlt645_mem.mod);
sys_run(&sys_meter_family, apps, N);
```

启动顺序：`board → infra → 适配器 → app_init(deps) → apps[] → sys_subscribe → sys_run`。

## 17.6 框架契约（D15）

`edge_module` 只提供：
- `edge_module_t`：生命周期状态 + 回调表（具体集合见 17.11）
- `edge_event_queue`：SPSC 环形队列（ISR push / 前台 pop）
- `edge_util`：无依赖头文件库

**不做**：注册表、装配、依赖解析、调度、服务定位。

## 17.7 事件模型

| 维度 | 结论 |
|---|---|
| 语义 | 事件 = **事实**（已发生）；命令走直接调用（D16） |
| 路由 | board/ISR 只 push 到**注入的 sink**；sys 按 event id 路由（D17） |
| 载荷 | `{id, source, arg0, arg1}` 固定标量，**禁原生指针**（D18） |
| 时间戳 | sink 入队时打单调时间戳；不做序号（D33） |
| 订阅 | `main()` 显式 `sys_subscribe(id, app)`（D32） |
| 编号 | 单一 `edge/events.h`，每模块 `0xNN00` 段（D31） |
| 背压 | 队列满 → 丢弃**最新**（保序）+ 计数（D54） |
| app 发布事实 | 允许 `sys_publish(ev)`，**仅 runner 上下文**；ISR/其它任务只能 push 到 sink（D54） |
| 退订 | 允许 `sys_unsubscribe`，**仅 runner 上下文**（D54） |
| 多订阅者 | 同一 event id 可多 app 订阅，按订阅顺序投递（D54） |

## 17.8 端口与适配器（Go 原则）

- 接口由 **app 定义**（使用者），infra 只给具体 API，适配器写产品 glue（D14/D24）
- 接口粒度：多个小接口，按需注入（D22）
- 存储：最小 KV（key → bytes）（D23）
- 设备：按能力拆窄接口（`byte_reader` / `byte_writer` / `relay_out`…）（D24）
- 时间：`clock_port_t`（D20）；日志：`log_port_t`（D41）
- 产品私有端口：允许，接口跟产品走（D13）
- 设备端口**实例/适配器**在组合根提供（D12）

## 17.9 错误模型（D19）

统一 `edge_status_t`：0 成功，负值 errno 风格。**需补**：写清 app / sys / board / infra 各自可返回的错误集合。

## 17.10 内存模型（D21）

调用者提供存储，**运行期零分配**。Go 的 `new` 在 C 的落地 = `xxx_init(self, deps)`。栈/静态/arena 由调用者决定。

## 17.11 生命周期与调度

**生命周期回调（D51）**：`edge_module_t` 只含
- `poll(ctx)`：周期调用
- `on_event(ctx, ev)`：事件投递
- `suspend(ctx)` / `resume(ctx)`：可选

`init`/`deinit` **不进统一结构**：由 `main()` 调 app 自己的 `xxx_init(self, deps)` / `xxx_deinit(self)`（装配期 / 关停期）。关停**逆序**（逆 priority）。

**调度（D52）**：事件驱动 + 周期轮询混合
- 每轮：先 drain 事件 → `on_event`；再按 `period` 调用到期的 `poll`
- 模块声明 `period`（tick；0 = 纯事件驱动）
- 执行预算：用 `clock_port` 测量，超预算**只告警 + 计数**（协作式，不抢占）
- 空闲：无事件且无到期 poll → `sys_idle()` → board 进低功耗
- `sys_run()` = `while(1) sys_step();`；`sys_step()` 供 RTOS 任务调用（D47）

顺序：priority 表 + `module_id` 升序 tie-break（D11）。

## 17.12 故障与低功耗分层（D9/D53）

| 类别 | 归属 |
|---|---|
| 硬件动作（喂狗/复位/断电/进出低功耗）| board |
| 软件策略（隔离/重启/降级）| sys |
| 模块状态（存/恢复）| app |

**策略（D53）**：
- 装配期 `init` 失败：默认**跳过并记录**；product 可把某模块标为致命 → 整机失败
- 运行期 `poll`/`on_event` 失败：计数 + 默认**隔离**（停止调度该模块，保留在列表，可配置重启）
- 看门狗：board 负责喂狗；sys 提供 `sys_health()`；"失败 N 次停喂狗/复位"的策略在 board 侧配置
`!` board 不得暴露 app 列表/模块状态。

## 17.13 认证边界（D45）

核心 = `edge_module` + `sys` + `board` + `infra`；**app 在核心外**。
边界靠源码/变更控制 + 核心 ABI + 固件签名保证，**非运行期沙箱**。沙箱（SEC-8）仅在未来的可加载模块工作流中需要。

## 17.14 构建系统与工程机制

| 项 | 结论 |
|---|---|
| 工具链 | CMake 驱动 `iccarm`；`.ewp` 仅 IDE 调试（D25） |
| 产品 target | `edge_add_product(name family board apps...)`（D26） |
| include 隔离 | 每模块独立 target + 精确 `target_include_directories`（D27） |
| 编译期校验 | 重复事件号/模块 ID、清单一致、层依赖方向（D28） |
| 体积预算 | CI 解析 map，设 flash/RAM 阈值（D37） |
| 可复现 | 固定工具链版本 + 记录 commit/工具链 hash（D38） |
| CI 矩阵 | 合法族×板组合全量构建 + 单测 + 两工具链（D39） |
| 清单一致 | `CMakeLists.txt` 与 `main.c` 的 app 列表必须一致 |

## 17.15 命名与编号

- 前缀：`edge_` / `sys_` / `board_` / `app_`（D29）
- 事件号：`0xNN00` 段，中央表 `edge/events.h`（D4/D31）
- 模块 ID 号段：中央 `edge/modules.h`，`0xNN00` 段，与事件号同构（D55）
- ABI/事件号：**只增不改、绝不复用**；版本号进 descriptor（D40）
- 持久化 schema：版本号 + 迁移函数（D42）

## 17.16 测试策略（D57）

- **app 单测**：mock 端口（填假 vtable）+ host 编译，**不带硬件**
- **sys 单测**：假 app
- **board / infra**：Renode 仿真（工区已有）或硬件在环
- **框架**：host 单测 + golden ABI
- **CI**：双工具链 + 族×板矩阵 + 体积预算（D25/D39/D37）

**详见第 18 节（先行）。**

## 17.17 两个独立立项

- **分发**（D34）：契约版本化、打包、兼容矩阵
- **合规**（D35）：TOE 证据、流程、文档、认证活动

## 17.18 待讨论清单

**已清零。** 原 9 项全部转为决策：
- 调度 → D52；生命周期 → D51；故障 → D53；事件细节 → D54
- board↔infra 边界 → 见 D46–D50 小节第 4 条
- 模块 ID → D55；ABI 版本对象 → D56；测试策略 → D57

## 17.19 决策索引

D1–D57 见文首《决策记录》。

## 17.20 平台抽象层（PAL）与并发模型（D46–D50）

```
裸机:   main() ──while(1)──┐
                          ├─→ sys_step()  ──→ app.poll / app.on_event
RTOS:   专用 runner 任务 ──┘        ↑
                                  事件队列
ISR / 其它任务 ──────────────────────┘   （只投递，不调 app）
```

PAL 原语（board 实现）：`pal_critical_enter/exit`、`pal_mb`、`pal_now`、`pal_in_isr`、`pal_isr_enter/exit`。

## 17.21 中立性矩阵

| 变化 | 只改哪里 | app 要改吗 |
|---|---|---|
| 换 SoC | board（+ PAL 实现） | 否 |
| 换 RTOS / 上 RTOS | product/main + PAL | 否 |
| 换驱动模型（寄存器/HAL/Zephyr/Linux） | infra | 否 |
| 换外设型号 | infra + glue | 否 |
| 换产品族 | sys | 否 |

这张表就是这套架构的最终验收标准：**前三行里 app 一律不改**。

---

# 18. 测试策略（D57 细化 · 先行）

> 测试策略先行，因为它反过来约束接口设计。本节的 T-A..T-F 是**设计硬约束**，不是测试细节。

## 18.1 为什么先行：可测性 = 设计约束（D58）

| 约束 | 内容 | 破坏后果 |
|---|---|---|
| T-A | 无全局可变状态（事件队列除外） | 测试间互相污染 |
| T-B | 依赖全部注入（消费者定义接口） | 无法替换假实现 |
| T-C | 时间只来自 `clock_port` | 时间逻辑无法确定性测试 |
| T-D | runner 可分解为 `sys_step()` | 无法逐拍驱动 |
| T-E | 运行期零分配 | 内存行为不确定，难复现 |
| T-F | PAL 可替换 | host 上跑不起来 |

**D14/D20/D21/D47 已经满足它们**；本节把它们固化为不可回退项。任何未来修改如果破坏 T-A..T-F，必须重新评估测试能力。

## 18.2 分层测试矩阵

| 层次 | host 单测 | Renode | HIL |
|---|---|---|---|
| edge_module 框架 | ✅ | — | — |
| app | ✅（mock 端口） | 可选 | 可选 |
| sys | ✅（假 app + 假时钟） | — | — |
| infra | 部分（纯逻辑） | ✅ | ✅ |
| board / PAL | ✗（含寄存器） | ✅ | ✅ |
| product 装配 | ✅（假 infra） | ✅ | ✅ |

## 18.3 Host 单测（主力，D59/D60/D61）

为什么能在 host 跑：消费者定义接口 ⇒ 假实现就是个 vtable；时间注入 ⇒ 假时钟；PAL 注入 ⇒ 空实现。

```c
/* app/dlt645/test/test_dlt645.c */
static edge_status_t fake_read(void *s, uint32_t k, void *b, size_t n) { ... }
static const dlt645_storage_if fake_storage = { fake_read, fake_write };

static void handles_rx_frame(void **state) {
    dlt645_t app;
    dlt645_init(&app, &fake_storage);
    clock_fake_advance(100);
    edge_event_t ev = { .id = EDGE_EVT_DLT645_RX, .arg0 = 0x68 };
    dlt645_module(&app)->on_event(dlt645_module(&app), &ev);
    assert_true(dlt645_frame_seen(&app));
}
```

- 框架：**cmocka**（D59）；缺 cmocka 直接**报错**，不静默跳过（修正 SDK 现有缺陷）。
- 假实现：**手写** vtable，集中 `test/fakes/`（D60），不用代码生成。
- 假时钟/假 PAL：host 提供可控实现（D61），让 `sys_step()` 可逐拍确定性驱动。
- host 上开 **ASan / UBSan / gcov**。

## 18.4 Renode（D62）

- 只测 **board / infra**（含寄存器与外设）
- 每 board 一个平台描述 `.resc`
- 用 Robot Framework 断言
- 可注入 UART 输入、GPIO、定时器中断；跑真实固件 ELF
- `!` app/sys **不在** Renode 测（host 更快、更可重复）

## 18.5 HIL（D63）

- 只测 Renode 建模不了的：模拟前端、计量芯片（RN8302 / AD777x 类）、真实时序与 EMI
- 最小集，作为最终验收，**不进日常 CI**

## 18.6 专项测试

- **Golden ABI**（D56）：`_Static_assert` 布局 + host 侧 sizeof/offset 校验
- **事件队列**：SPSC fuzz + 丢弃计数断言
- **失败注入**（D53）：init 失败 / 运行期失败 / 隔离策略
- **预算与超时**（D52）：假时钟推进，断言超预算计数
- **中立性验收**（17.21）：同一个 app 分别对 2 个 board / 2 种 RTOS 构建，**必须零改动**

## 18.7 目录约定

```
edge_module/test/
app/<name>/test/
sys/<family>/test/
infra/<name>/test/
board/<board>/test/        Renode 用例
test/fakes/                共享假实现
test/renode/*.resc *.robot
```

## 18.8 CI 流水线（D64）

1. **host 单测**：GCC + ASan/UBSan + gcov
2. **目标构建矩阵**：GCC cross + IAR；ABI 静态断言；体积预算（D37）
3. **Renode**：board/infra 用例
4. **夜间 HIL**：最小集

门槛：**中立性验收（18.6 末条）必须过**。

## 18.9 先行清单

- [ ] T1 定框架（D59：cmocka）并让 CI 在缺失时报错
- [ ] T2 定 fakes 约定 + `test/fakes/` 目录
- [ ] T3 定假时钟 / 假 PAL 接口
- [ ] T4 写第一个 host 单测骨架（app，无硬件）
- [ ] T5 接 CI 第 1 阶段（host 单测 + ASan）
- [ ] T6 定 Renode 平台描述模板
- [ ] T7 写中立性验收脚本（同 app × 2 board 构建）

## 18.10 T7 中立性验收（可直接实现的口径）

目的：用可执行的方式证明 17.21 矩阵。分三级。

**T7a 静态 include 图检查（快，每次提交）**
- 对 app 目标生成依赖（`gcc -M` / 编译器 `-H`）
- 断言：app 的**传递 include** 只允许出现 `app/<name>/`、`edge_module/`、工具链/标准头
- 出现 `board/`、`infra/`、`sys/`、SoC 头、RTOS 头 → **失败**
- 这条就是 D27/D28 的落地，比构建矩阵快得多

**T7b 目标构建矩阵（CI 门槛）**
- 参考 app：任选一个通用 app（如 `app/dlt645`）
- 矩阵：≥ 2 个 board × 2 种 runner 模型（baremetal / rtos）
- 每个组合构建一个**最小 product** 并链接该 app
- 三条断言：
  1. 所有组合构建成功、零 warning
  2. 构建前后 `git diff --exit-code app/<name>` **必须为空**（app 零改动）
  3. 每种组合下 T7a 的 include 图仍只含允许集合
- 脚本形状：

```bash
# tools/ci/neutrality_check.sh
set -e
APP=app/dlt645
for board in rn8615 gd32; do
  for model in baremetal rtos; do
    cmake -S tests/integration/minimal_product -B build/$board-$model \
          -DBOARD=$board -DRUNNER=$model -DAPP=$APP
    cmake --build build/$board-$model
  done
done
git diff --exit-code $APP      # app 源码不得改动
```

**T7c Host 任务模型（补 RTOS 语义，不进 RTOS 头）**
- 一个 pthread/线程反复调 `sys_step()`，另一线程/主循环当生产者投递事件
- 证明：`sys_step()` 分解真的能跑在多任务模型下，且队列无锁安全
- 它与 T7a 合起来代替"真跑一个 RTOS"，避免 CI 拖入整条 RTOS 构建链

通过 = 17.21 矩阵前两行（换 SoC / 换 RTOS）成立。

## 18.11 最小 product 测试夹具

T7b 需要一个极小的 product，作为可复用夹具（不是真产品）：
```
tests/integration/minimal_product/
    CMakeLists.txt    参数化 BOARD / RUNNER / APP
    main.c            baremetal: while(1) sys_step()
    runner_rtos.c     rtos 模型：任务调 sys_step()
    glue.c            把测试用假 infra 适配给 APP
```
`!` 它必须保持极小 —— 它一膨胀，中立性验收就不再是"最小证明"。

---

# 19. 残余讨论项

> 64 条决策已冻结，但逐行审阅后仍有 12 处未闭合。按紧急度分组。

## 19.1 第一优先（影响接口）

> ✅ R1 / R2 / R3 已定稿，详见第 20 节。

**R1 多 ISR 并发写队列**
多个外设 ISR 都要投递事件时，队列是：
- A ★ **每生产者一个 SPSC 队列**（sys 轮询多个）——无锁、最确定
- B 单队列 + PAL 临界区保护（ISR 里短暂关中断）——简单，但有嵌套/时延风险
- C 按 board IRQ 一个队列

我推荐 **A**：与 D18（禁指针）+ D47（单 runner）一脉相承，且彻底避开临界区。代价：M 个外设 M 个队列（内存换确定性）。

**R2 sys / board / sink 的接口汇总（方案目前最大的缺口）**
目前 `sys_run/sys_step/sys_subscribe/sys_publish/sys_idle/sys_health`、`board_init/board_irq_attach/pal_*`、"sink 类型"都只在正文里零散提及，**没有一份完整接口清单**。
→ 建议新增一节"接口目录（API 清单）"，把所有跳层函数签名列齐。

**R3 变长载荷怎么传**
D18 定了事件只有标量，但块数据（如 256 字节帧）怎么送？
- A ★ 事件带 token/index，app 通过 port 读 infra 缓冲区
- B 一个事件一个字节（简单，但事件风暴）
- C 事件内联小 buffer（需放宽 D18）

我推荐 **A**：守住 D18，大载荷走端口。

## 19.2 第二优先（影响行为）

> ✅ R4–R8 已定稿，详见第 21 节。

**R4 错误码分配**：D19 说"写清每层集合"，实际需要一份中央错误码表（像事件号）→ `edge/errors.h` + 号段。

**R5 runner 每步预算**：`sys_step()` 一次处理多少事件？无界=延迟不可控 → 建议**有界 + 可配置**。

**R6 重入**：`on_event` 里调 `sys_publish` 允许吗？→ 建议允许，但**入队延后**，设最大深度。

**R7 低功耗竞态**：判定 idle → 进入睡眠之间来了 ISR？→ 建议 PAL 提供"关中断→查空→睡眠"的原子序列。

**R8 时间回绕**：单调 tick 溢出时的 period 比较。

## 19.3 第三优先（可后定）

> ✅ R9–R12 已定稿，详见第 21 节。

- **R9** app 与 module 是否 1:1（现在默认 1:1）
- **R10** 诊断聚合接口 `sys_stats()`（丢弃/错误/预算/健康）
- **R11** ISR 有界规则的静态检查项
- **R12** bootloader / OTA 边界（明确 out-of-scope 还是单独立项）

---

# 20. 第一优先残项的定案（R1/R2/R3）

## 20.1 R1 多 ISR 并发写队列 → D65

结论：**每生产者一个 SPSC 队列**。

- 生产者 = 一个中断源 / 一个 infra 驱动（或 board 自身）
- 每个生产者拥有自己的 `edge_event_queue_t`（静态缓冲，容量可配）
- ISR 只 push；runner 只 pop
- sys 通过 `sys_attach_queue()` 登记所有队列；`sys_step()` 依次排空
- **不使用临界区**：嵌套/抢占的不同 IRQ 用的是不同队列，天然无竞争

"注入的 sink"（D17）正式落地为：**sink = 生产者自己的 `edge_event_queue_t`**。
- board 的 ISR 只调 `cb()`；`cb` 由 infra 提供，push 到 infra 自己的队列
- board 连队列都无需知道

代价：M 个生产者 = M 个队列（内存换确定性）。丢弃计数（D54）按队列独立。

## 20.2 R3 变长载荷 → D66

结论：**事件只带标量 + token；数据本体留 infra 缓冲区，app 通过端口读。**

- ISR/驱动把字节收进自己的缓冲区（环形 / DMA）
- 只投递"完成/到达"事件，带 token（如 index/len）
- app 在 `on_event` 里通过**自己定义的端口**读取数据
- 生命周期规则：缓冲区在 app 消费前有效；同 id 的下一个事件到达即失效

```c
/* 事件只说"RX 帧到了，共 len 字节" */
edge_event_t ev = { .id = EDGE_EVT_UART0_RX, .arg0 = len };
/* app 读数据 */
uint8_t buf[256];
dlt645_rx_if.read(dlt645_rx_if.self, buf, len);
```

## 20.3 R2 接口目录（API 清单）→ D67

四组：框架 / 运行时 / 平台 / 端口。app 自身接口由各 app 定义。

### 20.3.1 框架 `edge_module`

```c
/* edge/types.h */
typedef int32_t edge_status_t;

/* edge/event.h */
typedef struct {
    uint32_t id, source, arg0, arg1;
    uint32_t ts;                 /* 单调 tick，由 push 时填 */
} edge_event_t;

typedef struct {
    edge_event_t  *buf;
    uint16_t       capacity;
    volatile uint16_t head;      /* 仅生产者写 */
    volatile uint16_t tail;      /* 仅 runner 写 */
    volatile uint32_t drops;
} edge_event_queue_t;

void edge_event_queue_init(edge_event_queue_t *q, edge_event_t *buf, uint16_t cap);
bool edge_event_push(edge_event_queue_t *q, const edge_event_t *ev);  /* 生产者上下文 */
bool edge_event_pop(edge_event_queue_t *q, edge_event_t *ev);         /* 仅 runner */

/* edge/module.h —— 极薄模块头（嵌在 app 实例里） */
typedef struct edge_module edge_module_t;
struct edge_module {
    uint32_t       id;
    uint16_t       priority;
    uint32_t       period;       /* tick；0 = 纯事件驱动 */
    edge_status_t (*poll)(edge_module_t *self);
    edge_status_t (*on_event)(edge_module_t *self, const edge_event_t *ev);
    edge_status_t (*suspend)(edge_module_t *self);   /* 可选 */
    edge_status_t (*resume)(edge_module_t *self);    /* 可选 */
    void          *impl;         /* 指向 app 实例 */
};
```

### 20.3.2 运行时 `sys`（每族实现，接口统一）

```c
/* sys/sys.h */
typedef struct {
    uint32_t polls, events, drops, errors, budget_hits, isolated;
} sys_stats_t;

typedef struct sys sys_t;

sys_t       *sys_<family>_create(void);        /* 族提供 */
void         sys_attach_modules(sys_t *s, edge_module_t *const *mods, size_t n);
void         sys_attach_queue(sys_t *s, edge_event_queue_t *q);
edge_status_t sys_subscribe(sys_t *s, uint32_t event_id, edge_module_t *m);
edge_status_t sys_publish(sys_t *s, const edge_event_t *ev);   /* 仅 runner */
void         sys_step(sys_t *s);               /* 跑一轮 */
void         sys_run(sys_t *s);                /* while(1) sys_step(); 裸机用 */
void         sys_idle(sys_t *s);               /* 无事可做时调 → board 低功耗 */
const sys_stats_t *sys_stats_get(const sys_t *s);
```

> `sys_run` 与 `sys_step` 分开，是 D47（裸机/RTOS 共用）的落地。

### 20.3.3 平台 `board` + `PAL`

```c
/* board/board.h —— 每板实现，接口统一 */
void          board_init(void);                                   /* 时钟/引脚/复用/向量表 */
edge_status_t board_irq_attach(int irq, void (*cb)(void *), void *ctx);
void          board_irq_detach(int irq);
void          board_reset(void);
void          board_enter_low_power(void);
void          board_feed_watchdog(void);

/* edge/pal.h —— 框架声明，board 实现 */
void     pal_critical_enter(void);
void     pal_critical_exit(void);
void     pal_mb(void);
uint32_t pal_now(void);
bool     pal_in_isr(void);
void     pal_isr_enter(void);
void     pal_isr_exit(void);
```

`!` board 的接口里**不得出现** app 列表/模块状态（D9/D49）。

### 20.3.4 端口 `edge/ports.h`（可选规范形状，非强制）

> Go 里 `io.Reader` 是共享接口；这里提供一套规范的窄接口，app **可用可不用**，用了则互通性更好。

```c
typedef struct { edge_status_t (*read)(void *self, void *buf, size_t n); } edge_byte_reader_t;
typedef struct { edge_status_t (*write)(void *self, const void *buf, size_t n); } edge_byte_writer_t;
typedef struct {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t n);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t n);
} edge_storage_kv_t;
typedef struct { uint32_t (*now)(void *self); } edge_clock_t;
typedef struct { void (*write)(void *self, int level, const char *s); } edge_log_t;
```

### 20.3.5 app 形状（约定）

```c
/* app/<name>/<name>.h */
edge_status_t  <name>_init(<name>_t *self, ...deps);   /* 调用者提供存储 */
void           <name>_deinit(<name>_t *self);
edge_module_t *<name>_module(<name>_t *self);
```

### 20.3.6 product / main 形状

```c
board_init();
flash_t flash; flash_init(&flash);
dlt645_t app; dlt645_init(&app, &g_storage_if, &g_clock_if);

edge_module_t *mods[] = { dlt645_module(&app), ... };
sys_t *sys = sys_meter_family_create();
sys_attach_modules(sys, mods, 1);
sys_attach_queue(sys, &uart0_q);
sys_subscribe(sys, EDGE_EVT_UART0_RX, mods[0]);
sys_run(sys);          /* 裸机；RTOS 则 while(1) sys_step(sys); */
```

---

# 21. 第二、三优先残项的定案（R4–R12）

## 21.1 R4 错误码分配 → D68

中央 `edge/errors.h`，与事件号同构（D31/D55）：

```c
/* 基础值兼容 errno */
EDGE_OK=0, EDGE_EINVAL=-22, EDGE_ENOMEM=-12, EDGE_ESTATE=-200,
EDGE_ETIMEDOUT=-201, EDGE_ENOTSUP=-206, EDGE_EBUSY=-16, EDGE_EIO=-5 ...

/* 每模块错误段（与模块 ID / 事件号同一号段） */
#define EDGE_ERR(mod, code)  (-(int32_t)(((mod) & 0xFF00u) | ((code) & 0xFFu)))
```

- 框架用 `-1..-99`（镜像 errno）+ 保留区
- 每模块一个 `0xNN00` 段（与 D55 同构）
- **编译期唯一性检查**（同 D28）
- `edge_status_t` 全文统一，不分层式错误域

## 21.2 R5 runner 每步预算 → D69

`sys_step()` **有界**：
- 每个队列每步最多取 **K** 个事件（K 可配）
- 每个模块每步最多触发一次到期 `poll`
- 剩余事件留到下一轮（loop 很快会再进来）
- 与时间无关（不依赖时钟精度），因此确定性好；可选加时间护栏

`!` 因为 **poll 每步必跑**（不受事件数量影响），所以事件风暴不会饿死 poll。

## 21.3 R6 重入 → D70

- `on_event` 里调 `sys_publish` **允许**
- 但不得直接写 ISR 队列（那些队列的 producer 是 ISR，违反 SPSC 所有权）
- 而是写入 **runner 自有的待处理队列**，在本步末尾或下一步处理
- 设最大深度，超限丢弃 + 计数（并入 `sys_stats_t.drops`）

## 21.4 R7 低功耗竞态 → D71

经典丢失唤醒竞态：runner 判空 → 睡下之间来了 ISR。
解法：**原子序列由 board/PAL 负责**：
```
pal_critical_enter();
if (sys_is_idle(sys)) board_enter_low_power();   /* WFI / STOP */
pal_critical_exit();
```
- 裸机：关中断→复查→WFI（任何 pending 中断都会唤醒）→开中断，ISR 随后执行，不丢事件
- RTOS：交给 RTOS 的 idle/wakeup 机制（`sys_idle` 仅抛钩子）

## 21.5 R8 时间回绕 → D72

tick 是 `uint32_t` 单调、会回绕。所有比较一律用模运算安全写法：
```c
if ((int32_t)(now - due) >= 0) { /* 到期 */ }
```
**禁止** `if (now >= due)`。这条写进评审检查项。

## 21.6 R9 app 与 module 的关系 → D73

**一个 app 可暴露 1..n 个 `edge_module_t`**（默认 1）。
例：一个 `uart` app 管理 4 路口 → 暴露 4 个 module（各自 id/priority/period）。
`!` 不是硬性 1:1；但同一 app 的多个 module 共享一个实例时，`on_event` 要知道是哪个口（用 `ev->source`）。

## 21.7 R10 诊断聚合 → D74

- `sys_stats_get()` 返回全局聚合：`polls / events / drops / errors / budget_hits / isolated`
- 每模块计数**可选**（放 module 自身，或 sys 内的固定数组）
- **不引入**额外的数据上报基础设施；需要 shell/RTT 时由 product 读取后自行输出

## 21.8 R11 ISR 有界规则 → D75

ISR（`board` 转发的 `cb`）**只允许**：
1. 清中断标志
2. `edge_event_push()`
3. 可选的 `pal_now()`

**禁止**：循环遍历数据、动态分配、阻塞、调用任何 `app`/`sys` 函数、打日志（可计数后用事件上报）。
落地：评审清单 + 静态检查（ISR 标记函数内只允许白名单调用）。

## 21.9 R12 bootloader / OTA 边界 → D76

**本方案 out-of-scope**，单独立项。
本方案只提出两条要求以便后续 OTA 可嫁接：
1. 核心 ABI 稳定 + 版本号（D40/D56）
2. 固件签名/防回滚由 bootloader 负责，不在本核心内实现

---

# 22. 方案闭合状态

| 项 | 状态 |
|---|---|
| 决策 | **D1–D76** |
| 悬空项 | **0**（R1–R12 全部转决策） |
| 待落地任务 | S2–S8（第 15 节）、T1–T7（第 18.9 节） |
| 独立立项 | 分发（D34）、合规（D35）、bootloader/OTA（D76） |

**方案至此完整闭合，可据此开写代码。**

---

# 23. 资源管理与 app 间交互（D77–D84）

## 23.1 四类资源与所有权（D77）

| 资源 | 所有者 | 访问方式 |
|---|---|---|
| 硬件（引脚/时钟/复用/中断向量）| board | 仅 board；infra 通过 `board_irq_attach()` 登记 |
| 设备（UART/存储/时钟/日志服务）| infra | 消费者定义接口 + 组合根适配器 |
| 内存 | product（调用者提供，D21）| 构造时传入 |
| 事件队列 | 生产者（D65）| 只有生产者 push |

## 23.2 app 间交互只有两条路（D78/D79）

**1. 事件（事实）**——B 广播，A 订阅。单向、松耦合。

**2. 服务接口（请求/响应）**——B 暴露 provider API → 组合根写适配器 → A 按**自己定义的** consumer 接口调用：
```c
/* B（提供方）：只暴露具体 API */
edge_status_t meter_data_get(meter_t *m, meter_snapshot_t *out);

/* A（使用方）：自己定义需要的形状 */
typedef struct { edge_status_t (*get)(void *self, meter_snapshot_t *out); } meter_snapshot_if;

/* 组合根 glue：适配 */
static edge_status_t ad_get(void *self, meter_snapshot_t *o) { return meter_data_get(self, o); }
static const meter_snapshot_if g_if = { ad_get };
```

**关键洞察**：**"app 提供资源"与"infra 提供资源"在架构上完全一样** —— app 只是另一种 provider。因此资源管理不需要新机制，直接复用"消费者定义接口 + 组合根"。

**禁止**：直接 include 对方头、全局变量、服务定位器。

## 23.3 无锁是单 runner 的推论（D80）

- D47 ⇒ 所有 app 代码在**同一上下文串行** ⇒ runner 内无并发 ⇒ **app 间访问共享资源不需要锁**
- 唯一并发是 ISR vs runner，而 D75 规定 ISR 不碰 app 资源
- 因此最初发散里的 `RES-1 资源仲裁器` **不需要独立存在**

## 23.4 独占与共享（D81/D84）

- **独占资源**（一个 UART 给一个 app）：组合根只把该适配器给一个所有者
- **共享设备**（总线）：由 infra 驱动自己串行化/复用，**不在 app 之间协调**
- **共享中断线**：`board_irq_attach()` 可对同一 IRQ 多次调用；board 按注册序分发；每个 handler 有自己的队列（D65），因此仍无竞争

## 23.5 依赖顺序与缺失依赖（D82/D83）

- **顺序**：组合根显式构造顺序就是依赖顺序（D5）；关停逆序（D51）
  ```c
  meter_init(&meter);                 /* B 先 */
  dlt645_init(&dlt645, &g_meter_if);  /* A 后 */
  ```
- **缺失依赖**：若 B 不在本产品里，组合根的 glue 必须为 A 的接口提供一个**替代实现**（空实现/降级实现）。这是产品变体的一部分，**在编译期就会暴露**（缺失适配器 → 链接/编译失败）

## 23.6 与最初 RES-1..4 发散的对应

| 原发散 | 现在 |
|---|---|
| RES-1 资源仲裁器 | 不需要（D80）| 
| RES-2 依赖满足校验 | 编译期：缺适配器即失败（D83）|
| RES-3 所有权/冲突检测 | 组合根保证 + 共享中断多 handler（D81/D84）|
| RES-4 生命周期绑定 | main 控制构造/析构，逆序（D82）|

---

# 24. 纳入多 SoC / 多外设 / 多驱动模型 / 多 RTOS 后的目录结构

> 本节是 17.4 的完整版。每种目录对应它吃掉的多样性，标注在右侧。

```
edge-module-sdk/
│
├── edge_module/                       框架（极薄，与平台/RTOS 无关）
│   ├── include/edge/
│   │   ├── types.h                    edge_status_t
│   │   ├── module.h                   edge_module_t
│   │   ├── event.h                    edge_event_t / edge_event_queue_t
│   │   ├── events.h                   事件号分配表（中央，0xNN00 段）
│   │   ├── errors.h                   错误码分配表（中央，同构）
│   │   ├── modules.h                  模块 ID 分配表
│   │   ├── ports.h                    可选规范窄接口（reader/writer/kv/clock/log）
│   │   ├── pal.h                      PAL 接口声明
│   │   └── util/                      ringbuf / crc / bit（无依赖头）
│   └── src/
│
├── pal/                     ← 吃「各种 RTOS」  平台抽象层：按 (架构 × RTOS) 组合
│   ├── cortex-m-bare/                 PRIMASK 临界区，无 ISR 桥接
│   ├── cortex-m-freertos/             portSET_INTERRUPT_MASK + portYIELD_FROM_ISR
│   ├── cortex-m-zephyr/               irq_lock + z_swap
│   ├── riscv32-bare/
│   └── host/                          host 假 PAL（临界区=空，pal_now=假时钟）
│
├── soc/                     ← 吃「各种 SoC」   SoC 支持包（寄存器定义 / 厂商 HAL / SoC 级驱动）
│   ├── rn8xxx/
│   │   └── uart_reg_rn8615.c          寄存器级驱动（SoC 绑定 → 住这里）
│   ├── gd32f4/
│   │   └── uart_hal_gd32.c            厂商 HAL（SoC 绑定 → 住这里）
│   ├── stm32h7/
│   └── t536/
│
├── board/                   ← 吃「各种板子 × 各种外设」  每板一份
│   ├── rn8615_meter/
│   │   ├── board.h                    board_init / board_irq_attach / 低功耗 / 看门狗
│   │   ├── board.c
│   │   ├── vectors.c                  向量表（唯一拥有者）
│   │   └── pins.c                     引脚 / 复用 / 时钟树
│   ├── gd32f470_evb/
│   └── t536_relay/
│
├── infra/                   ← 吃「各种设备」  只放可移植核心 + 窄端口，不认 SoC / 不认 OS
│   ├── uart/
│   │   ├── uart_core.c                协议 / 缓冲 / 策略，只依赖窄端口
│   │   └── uart_mock.c                host 假实现
│   ├── flash_kv/
│   ├── clock/
│   ├── log/
│   └── relay/
（寄存器级驱动→`soc/<soc>`；OS 设备模型如 Zephyr→`pal/<os>`；绑定→`product/<name>/glue`）
│
├── sys/                             产品族运行时（每族一份实现）
│   ├── sys.h                        统一接口：sys_run/step/subscribe/publish/idle/stats
│   ├── meter_family/
│   └── relay_family/
│
├── app/                             应用模块（零具体依赖，跨产品复用）
│   ├── dlt645/
│   │   ├── include/dlt645/           自己定义的接口 + init 声明
│   │   ├── src/
│   │   └── test/                     host 单测 + 本 app 的 fakes
│   ├── dlms/
│   ├── relay/
│   └── lcd/
│
├── product/                         组合根：装配 + 适配器 + main
│   ├── meter_overseas/
│   │   ├── CMakeLists.txt            edge_add_product(name family board apps...)
│   │   ├── main.c                    显式装配（baremetal）
│   │   ├── runner_rtos.c             RTOS 任务版：while(1) sys_step()
│   │   ├── glue/                     适配器：infra/app → 各 app 定义的接口
│   │   ├── apps/                     产品私有 app
│   │   └── config/
│   └── relay_t536/
│
├── test/                            共享测试设施
│   ├── fakes/                        共享假实现
│   ├── minimal_product/              T7b 中立性验收夹具（参数化 BOARD/RUNNER/APP）
│   └── renode/                       *.resc + *.robot
│
├── tools/
│   ├── cmake/edge_add_product.cmake
│   ├── cmake/iar-toolchain.cmake
│   └── ci/neutrality_check.sh
│
└── docs/
```

## 24.1 相对基础版的五处变化

| 新增 | 吃掉哪种多样性 |
|---|---|
| `pal/`（按 **架构×RTOS** 组合） | 各种 RTOS（裸机/FreeRTOS/Zephyr/RISC-V） |
| `soc/`（厂商支持包） | 各种 SoC |
| `board/` 拆出 `vectors.c` / `pins.c` | 各种板子 × 各种外设引脚 |
| `soc/<soc>/` 承载寄存器与 HAL 驱动，`pal/<os>/` 承载 OS 设备模型，`infra/` 只留可移植核心 | **各种设备驱动模型**（寄存器/HAL/Zephyr/Linux），且 infra 绝不反向认识 SoC |
| `product/<名>/glue/` + `runner_rtos.c` | 每个产品的装配与运行模型 |

## 24.2 对 D46 的修正（D85）

D46 原话是"PAL 由 board 实现"。纳入多 SoC / 多 RTOS 后，这样会导致**每块板重复实现 PAL**。

修正：**`pal/` 独立成层，按 (架构 × RTOS) 组合**，board 只是**选择**一个 PAL：

```
board/rn8615_meter  ──选──►  pal/cortex-m-bare   （裸机产品）
board/rn8615_meter  ──选──►  pal/cortex-m-zephyr （同一块板跑 Zephyr）
```

于是"同一块板换 RTOS"只换 PAL 引用，board 的引脚/时钟代码一行不动。

## 24.3 一个产品的选择矩阵（装配时确定）

```
product/meter_overseas
   ├── sys      = sys/meter_family
   ├── board    = board/rn8615_meter
   │      ├── soc = soc/rn8xxx
   │      └── pal = pal/cortex-m-bare
   ├── infra    = uart_core + flash_kv + clock + log
   │              ↑ infra 只提供可移植核心；换驱动模型只改下面的 soc/pal 与 glue
   ├── soc      = soc/rn8xxx（uart_reg_rn8615）
   ├── app      = app/dlt645 + app/dlms + app/lcd
   └── runner   = baremetal（main.c）/ rtos（runner_rtos.c）
```

**验证标准（17.21）**：把 `app/dlt645` 从 `meter_overseas` 搬到 `relay_t536`（换 SoC + 换板 + 换驱动模型 + 换 RTOS），`app/dlt645/` **一行不改**，只有 `product/*/glue/` 跟着换。
