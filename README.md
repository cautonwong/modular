# 模块化系统

> **Architecture / TODO Source of Truth**
>
> 根目录 `todo.md` 是完整架构决策与实施清单；README 保持与其核心原则一致，并记录当前已经落地的代码边界。

## 目标

面向**前后台（foreground/background）**嵌入式环境，不依赖 RTOS、线程或运行期动态分配。

```text
sys/<family>          board/<board>
       \                 /
        \               /
          product/<name>
       Composition Root
          /         \
         /           \
   app/<name>     infra/<name>
         \           /
          \         /
           edge_module
```

- `edge_module`：极薄框架契约；生命周期、事件队列、公共小工具。
- `sys`：每个产品族一个；负责排序、启动/回滚、事件路由、foreground 调度和关停策略。
- `board`：每块板一个；IRQ 清理/最小采集/事件转发及安全硬件动作，不认识业务 app。
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

## 目录

```text
edge_module/
    include/edge/module.h
    include/edge/event.h
    include/edge/events.h
    include/edge/clock.h
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
infra/<name>/
    include/<name>/*.h
    src/*.c
product/<name>/
    glue.c / glue/*.c
    main.c
```

依赖方向：

```text
app     -> edge_module
sys     -> edge_module
board   -> edge_module
infra   -> edge_module
product -> 全部
```

## 生命周期与 super-loop

```text
construct
   -> sys_init
   -> validate required
   -> app.init()          （失败自动回滚）
   -> running
   -> event dispatch
   -> app.poll()          （bounded / cooperative）
   -> power_off           （逆序）
   -> deinit              （逆序）
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
- **产品线组合矩阵**：`edge_add_product(name family board infra apps)` 从注册表解析，构建 `example` / `meter_host` / `meter_mps2` 三个合法产品并运行；非法 family/board/app/infra 组合由 CMake 拒绝（CI 反例）
- 架构守卫：app 依赖边界（含 app→app / 具体层 / RTOS）、中央事件号唯一性、CMake↔main app 清单一致性，并带**反例自测**
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
- [x] CMocka event/sys tests
- [x] GCC/Clang/sanitizer/coverage CI
- [x] product Flash/RAM size gate

待实施：

- [ ] scheduler 周期分频 / execution budget / idle-low-power
- [ ] 更细的 fault policy 与 diagnostics
- [ ] Renode/HIL IRQ 测试
- [ ] family × board × app-set matrix
- [ ] GCC + IAR/iccarm CMake toolchain
- [ ] reproducible-build metadata
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

以根目录 `todo.md` 作为 D1-D45 决策、边界规则和后续实施清单的完整记录。
