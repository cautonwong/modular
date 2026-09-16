# 模块化系统

> **Architecture / TODO Source of Truth**
>
> 当前架构以仓库根目录 `todo.md` 为完整设计记录；本 README 是其面向仓库首页的执行版摘要。两者共同描述同一套冻结架构。

## 目标

这是一个面向前后台（foreground/background）嵌入式环境的软件产品线架构，不依赖 RTOS、线程或运行期动态分配。

```text
             sys/<family>          board/<board>
                   \                 /
                    \               /
                    product/<name>
                 Composition Root
                    /             \
                   /               \
             app/<name>         infra/<name>
                    \               /
                     \             /
                       edge_module
```

- **sys**：每个产品族一个；负责运行期调度与策略。
- **board**：每块板一个；负责 IRQ 转发和硬件动作，不认识业务 app。
- **app**：跨产品复用的业务模块；只依赖 `edge_module` 和自己定义的消费者接口。
- **infra**：具体存储、通信、设备等实现。
- **product**：唯一组合根，负责选型、构造、adapter、连接和启动。

## 已冻结的核心原则

1. **显式装配**：`main()` 明确构造 app；不依赖链接段自动注册。
2. **构造注入**：`xxx_init(self, deps)`，禁止 service locator / `get_service()`。
3. **消费者定义接口**：接口跟随 app；infra 只提供自己的具体 API；adapter 位于 `product/<name>/glue`。
4. **零具体依赖**：app 不 include 其它 app、具体 infra、board/HAL/寄存器。
5. **sys 只调度**：按 `priority` 排序，同 priority 按 `module_id` 升序。
6. **board 只做板级工作**：ISR 清状态、最小采集、push event；绝不调用 app callback。
7. **事件只有事实语义**：命令使用直接 contract 调用。
8. **事件 payload 固定标量**：`{id, source, arg0, arg1, timestamp}`，禁止原生指针。
9. **运行期零分配**：对象、队列、buffer 由调用者提供。
10. **产品事实来源只有 CMake + main()**：不引入第三套 manifest。

## 目录

```text
edge_module/
    include/edge/module.h
    include/edge/events.h
    include/edge/clock.h
    include/edge/log.h
    include/edge/util/
    src/
app/<name>/
    include/<name>/
    src/
sys/<family>/
board/<board>/
infra/<name>/
product/<name>/
    glue/
    main.c
    apps/
```

依赖方向严格为：

```text
app     -> edge_module
sys     -> edge_module
board   -> edge_module
infra   -> edge_module
product -> 全部
```

## 运行模型

```text
board IRQ
   |
   v
injected event sink
   |
   v
bounded SPSC queue
   |
   v
sys router -- explicit subscription --> app.on_event()
   |
   v
app.poll()  (cooperative / bounded)
```

启动：

```text
board init
  -> infra init
  -> product glue
  -> app_init(deps)
  -> apps[]
  -> sys_subscribe()
  -> sys loop
```

## Port / Adapter 示例

```c
typedef struct {
    edge_status_t (*read)(void *self, uint32_t key, void *buf, size_t len);
    edge_status_t (*write)(void *self, uint32_t key, const void *buf, size_t len);
} dlt645_storage_if;

int dlt645_init(dlt645_t *self, const dlt645_storage_if *storage);
```

`dlt645` 定义它真正需要的接口；`infra/flash` 不需要知道 `dlt645_t`；`product/glue` 把两者连接起来。

## 工程质量门

CI 已包含：

- GCC Debug / Release
- Clang Debug / Release
- GCC ASan + UBSan
- Clang ASan + UBSan
- `-Wall -Wextra -Wpedantic -Werror`
- CMocka unit tests
- clang-tidy
- cppcheck
- architecture dependency guards
- coverage / gcovr artifact
- PR、main push、手动触发
- concurrency cancellation
- failure diagnostics artifact

后续 CI 必须继续扩展到合法的 **family × board** 产品矩阵、双工具链（GCC + IAR/iccarm）、map Flash/RAM budget 和 reproducible-build metadata。

## 当前实施重点

- [x] monorepo 基础结构
- [x] 显式 composition root 示例
- [x] constructor injection 示例
- [x] priority + `module_id` tie-break
- [x] required module 校验接口
- [x] board IRQ -> injected sink
- [x] bounded event queue + drop counter
- [x] central event ID header
- [x] explicit `sys_subscribe()` / event routing
- [x] injected clock/log port contracts
- [x] GCC/Clang + sanitizer CI matrix
- [x] static analysis + coverage
- [ ] 完成 D15 极薄 framework 的最终收缩
- [ ] 完成 CMake `edge_add_product()` 与清单一致性检查
- [ ] 完成 IAR/iccarm CMake toolchain
- [ ] 完成产品 family × board matrix
- [ ] 完成 map Flash/RAM budget gate
- [ ] 完成 Renode/HIL IRQ 测试
- [ ] 完成 scheduler 周期、预算、低功耗和 fault isolation
- [ ] 完成 ABI/contract compatibility matrix

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
- 为当前模块系统重新引入 RTOS/thread 模型

## 完整设计

请以仓库根目录的 [`todo.md`](todo.md) 作为完整架构、D1-D45 决策和实施清单。