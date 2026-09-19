# 模块化系统

> **Architecture source of truth**
>
> [`docs/adr.md`](docs/adr.md)（D1-D85）是唯一决策源；[`docs/adr-conformance.md`](docs/adr-conformance.md)
> 是决策与实现的唯一差异视图。根目录 `todo.md` 仅作历史存档。

## 目标

面向**前后台（foreground/background）**嵌入式环境，不依赖 RTOS、线程或运行期动态分配。

```text
board/<board> ──selects──▶ soc/<soc> + pal/<arch × RTOS>
      │
      │        sys/<family>
      └────┬────────┘
           ▼
    product/<name>   Composition Root
          /       \
    app/<name>   infra/<name>
          \       /
         edge_module
```

- `edge_module`：极薄框架契约；生命周期、事件队列、公共小工具。
- `sys`：每个产品族一个；负责排序、启动/回滚、事件路由、foreground 调度和关停策略。
- `board`：每块板一个；IRQ 清理/最小采集/事件转发及安全硬件动作，不认识业务 app。
- `soc`：SoC 支持包（厂商内存映射/IRQ 号）；由 board/infra 选择，不反向依赖框架或板。
- `pal`：平台抽象（架构原语、RTOS 宿主）；由 board/product 选择。
- `app`：可复用业务模块；只依赖 `edge_module` 与自己定义的消费者接口。
- `infra`：具体基础设施实现；不依赖 app 业务类型。
- `product`：唯一组合根；选择 family/board/infra，构造 app，连接 adapter，启动产品。

## 已冻结原则

1. `main()` 显式构造 app；不使用链接段自动注册。
2. 构造注入，禁止 service locator / `get_service()`。
3. 接口跟随消费者；adapter 位于 product glue。
4. app 不依赖其它 app、具体 infra、board/HAL/寄存器。
5. sys 按 `priority` 调度；同 priority 按 `module_id` 升序。
6. required 与 priority 分离；required 单独校验。
7. ISR 不调用 app callback，只进入注入的 event sink。
8. event payload 固定 `{id, source, arg0, arg1, timestamp}`，禁止裸指针。
9. 队列容量由调用者提供；满时 drop-newest 并累计 counter。
10. 时间通过 `edge_clock_port_t` 注入；sink 入队时写 monotonic timestamp。
11. 运行期零 malloc/free；对象和 buffer 由组合根提供。
12. 产品事实来源只有 CMake + `main()`，不引入平行 manifest。
13. 每个 product 显式绑定**唯一** board，且绑定不可被构建参数覆盖；`board` 绑定唯一 `soc`（D86）。
14. `infra` 只依赖窄端口：**绝不**依赖 `soc`。寄存器/HAL 驱动归 `soc/<soc>`，OS 设备模型归 `pal/<os>`，绑定归 product glue（D48）。

## 目录

```text
edge_module/
    include/edge/module.h
    include/edge/event.h
    include/edge/events.h
    include/edge/modules.h
    include/edge/errors.h
    include/edge/ports.h
    include/edge/clock.h
    include/edge/log.h
    include/edge/pal.h
    src/event.c
app/<name>/
    include/<name>/*.h
    src/*.c
sys/<family>/
    include/<family>/sys.h
    src/*.c
board/<board>/
    include/<board>/*.h
    src/*.c
soc/<soc>/
    include/<soc>/*.h        SoC support package (memory map, IRQ numbers)
pal/<arch×os>/
    include/.../*.h          platform abstraction (arch primitives / RTOS host)
infra/<name>/
    include/<name>/*.h
    src/*.c
product/<name>/
    glue.c / glue/*.c
    main.c
tests/integration/minimal_product/   neutrality fixture (board × runner)
```

依赖方向：

```text
edge_module -> 仅标准/工具链头
app     -> app/<self> + edge_module
sys     -> sys + edge_module
board   -> board + soc + pal + edge_module
infra   -> infra + pal + edge_module     （默认禁止 infra -> soc）
soc     -> soc                （不反向依赖框架/板）
pal     -> pal + edge_module
product -> 全部
```

该矩阵由 `.github/scripts/check_layer_dependencies.py` 在 CI 中强制执行（解析 include 到实际归属层，反例自测见 `tests/guards/layer_*`）。**通用 infra 绝不绑定具体 SoC**：`infra -> soc` 无例外，寄存器/HAL 驱动住 `soc/<soc>`，OS 设备模型住 `pal/<os>`，绑定由组合根 glue 完成（D48）。

## 生命周期与 super-loop

```text
construct                     （组合根：<app>_construct(self, deps)）
   -> <app>_init(self)        （D51：组合根显式调用；失败策略归产品）
   -> sys_init
   -> validate required
   -> running
   -> event dispatch
   -> app.poll()          （bounded / cooperative）
   -> power_off           （sys 逆序调用，模块保存状态）
   -> <app>_deinit(self)      （D51：组合根逆序调用）
```

当前 sys 已实现：

- priority + module_id deterministic ordering
- duplicate module ID rejection
- explicit required-module validation
- init failure rollback
- event subscription/routing
- failed app isolation from后续 poll/dispatch
- reverse-order power-off/deinit
- caller-owned storage

## IRQ / Event

```text
hardware IRQ
    |
    v
board ISR
    |  clear IRQ + minimal capture
    v
edge_event_sink
    |  timestamp + bounded queue
    v
sys router
    |  explicit sys_subscribe(event_id, app)
    v
app.on_event()
```

事件队列本身采用无动态分配的环形缓冲。`edge_event_push_isr()` 面向单生产者；如果多个 IRQ 共享一个 sink，board 必须注入 `edge_irq_guard_t`，由平台实现临界区序列化生产者。框架本身不包含任何架构相关关中断代码。

中央事件号表位于 `edge_module/include/edge/events.h`，同时有 `_Static_assert` 与 CI collision checker。

## Consumer-defined Port / Adapter

例如 DLT645 定义自己需要的 KV 存储能力：

```c
typedef struct dlt645_storage_if {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
    void *self;
} dlt645_storage_if_t;
```

`infra/flash` 只提供自己的 `flash_read/write()`；`product/example/glue.c` 把二者适配起来。app 不 include infra 实现头。

## 构建

```cmake
edge_add_product(
    example
    family example
    board example
    apps dlt645
)
```

产品入口是 `product/example/main.c`，没有自动注册和隐藏依赖。

## CI/CD 质量门

当前 CI（`.github/workflows/ci.yml`）包含：

- 构建矩阵：GCC / Clang × Debug / Release、GCC / Clang ASan + UBSan
- `-Wall -Wextra -Wpedantic -Werror`
- CMocka 单元测试（事件、调度、app 契约、集成、PAL、GPIO），产出 JUnit 报告
- 覆盖率门禁（gcovr `--fail-under-line 95`，当前 ~99.8%）+ XML/HTML 产物
- clang-format 格式门禁、clang-tidy（`.clang-tidy`，warnings-as-errors）、cppcheck
- **产品线组合矩阵**：`edge_add_product(name family board infra apps)` 从注册表解析并校验**合法 family×board 白名单**，构建 `example` / `meter_host` / `meter_mps2` 等合法产品并运行；未知 family/board/app/infra、非法族×板组合，以及**重复注册 / 跨板重绑同一 product** 均由 CMake 拒绝（CI 反例）
- 架构守卫：app 依赖边界（app→app / 具体层 / RTOS / **SoC 头 / 裸寄存器 N3 / 重 libc N5**）、中央事件号唯一性、**中央模块号唯一性**、CMake↔main app 清单一致性，并带**反例自测**
- Cortex-M0 / Cortex-M4 交叉编译 + Cortex-M4 MPS2 QEMU 外设中断冒烟 + ELF 架构校验
- **map 文件级分层预算**（`edge_module/sys/board/infra/app/product/startup/other`）+ ELF Flash/RAM 总量门
- **可复现固件**（两次构建逐字节比对）+ provenance metadata + SBOM + SHA256SUMS
- 主机 PAL 契约实现（`pal/host`）与事件 sink 集成测试；IAR/iccarm 工具链文件（`cmake/toolchains/iar-arm.cmake`）
- Actions 全部按 commit SHA 固定、Dependabot 每周更新、CodeQL C/C++ 代码扫描
- tag 触发 Release 流水线（`.github/workflows/release.yml`）：校验和 + release 附件
- PR / main push / manual 触发、并发取消、失败诊断 artifact、`ci-success` 汇总门

下一阶段：Renode/HIL（需自定义 MPS2/CMSDK 平台描述与真实台架）、IAR/iccarm 授权 CI job、family/app 维度扩充（dlms）、ABI/contract 兼容矩阵。

## 当前状态

已落地：

- [x] monorepo 分层
- [x] thin `edge_module`
- [x] explicit composition root
- [x] consumer-defined port + product adapter
- [x] deterministic scheduler ordering
- [x] required validation
- [x] transactional init rollback
- [x] reverse shutdown
- [x] bounded event queue + drop accounting
- [x] injected monotonic clock
- [x] multi-IRQ guard contract
- [x] central event IDs + static/CI checks
- [x] **central module IDs（`edge/modules.h`）+ 编译期/CI 唯一性**
- [x] **central error table（`edge/errors.h`）+ 规范窄接口（`edge/ports.h`）**
- [x] CMocka event/sys tests
- [x] GCC/Clang/sanitizer/coverage CI
- [x] product Flash/RAM size gate
- [x] **period/budget 调度 + execution-budget 计数 + fault isolation**
- [x] **`suspend`/`resume` 生命周期回调与 `suspend_all`/`resume_all`**
- [x] **runner 内 `edge_sys_publish`（延后投递 + 深度上限 + drop 计数）**
- [x] **`edge_sys_step`/`edge_sys_run` 分解 + `edge_sys_idle` 空闲钩子**
- [x] **非致命 init 失败默认跳过并记录，`fatal` 模块才整机失败**
- [x] **stats 高水位 / `edge_sys_stats_reset`**
- [x] **合法 family×board 白名单 + 负例 CI**
- [x] **app 隔离守卫覆盖 N3（裸寄存器）与 N5（重 libc）**

待实施：

- [ ] `board_enter_low_power` / `board_feed_watchdog` 与 idle 钩子的板级接线（`pal/cortex-m-bare` 仍缺，当前仅 `pal/host`）
- [ ] scheduler 周期分频 / 每模块高水位诊断
- [ ] 多生产者 SPSC 队列（当前为单队列 + 注入 IRQ guard）
- [ ] Renode/HIL IRQ 测试
- [ ] family × board × app-set 全量枚举矩阵
- [ ] GCC + IAR/iccarm CI toolchain（工具链文件已备，未进 CI）
- [ ] ABI/contract compatibility matrix
- [ ] SDK distribution / compliance 独立项目
- [ ] persistence schema + migration

## 明确禁止

- 链接段自动注册作为产品装配来源
- service locator
- app -> infra 具体实现依赖
- app 直接访问寄存器
- board 调 app callback
- sys 构造具体 app
- product glue 编写业务逻辑
- app 间隐式直接依赖
- event / command 语义混用
- 运行期 malloc/free
- manifest 与 CMake/main 平行定义产品
- 为当前系统重新引入 RTOS/thread 模型

## 完整设计

- `docs/adr.md`：**唯一决策源**，D1-D85（含 PAL / 并发 / 多驱动模型 / 中立性蓝图）。
- `docs/adr-conformance.md`：决策与实现的唯一差异视图（含未闭合项与 wontfix）。
- 根目录 `todo.md`：历史存档（早期 D1-D45 冻结子集），不再更新。
