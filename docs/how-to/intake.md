# 接入外来代码（legacy / 厂商 SDK / 预编译库）

当搬进来的代码**不是**照这套规则写的时用这一篇：既有产品代码、厂商 HAL、第三方库、
预编译库。其它五篇 `docs/how-to/` 讲的是"按规则写新代码"，这一篇讲"把不按规则的代码
搬进来，并且不让它把规则撕开一个洞"。

先读 [`../governance.md`](../governance.md)（层矩阵与 ADR→门禁规则）。本篇不发明新
机制：只用 `RAW`（[`cmake/EdgeTargets.cmake`](../../cmake/EdgeTargets.cmake) 里的质量门
豁免）与 D14（适配器住组合根），并给它们配**规程、登记与 policy**。

## 每个外来单元必须回答四个问题

一个"外来单元"= 一个源文件、一个目录、或一个预编译库。**四个问题的答案要写进 PR
描述**，否则评审无法判断这堆代码该不该进来。

### 1. 落点：按"它认识谁"决定，不按"它原来在哪"

| 它认识什么 | 落哪一层 | 依据 |
|---|---|---|
| 只认识自己的协议/算法，不认识芯片与 OS | `app/<name>/` 或 `infra/<device>/` | D3/D15/D48 |
| 寄存器、厂商外设库、HAL | `soc/<soc>/` | D49（`infra -> soc` 无例外被禁） |
| OS 设备模型（Zephyr/Linux 驱动） | `pal/<os>/` | D85 |
| 板级引脚/时钟/向量/低功耗动作 | `board/<board>/` | D3/D9 |
| 只认识"装配"（把谁接给谁） | `product/<name>/`（glue/ 适配器） | D14 |

放不进去是一个信号：先问"它是不是同时认识两层以上"。如果它既碰寄存器又做业务，那
它必须**被切开**，而不是被塞进某一层。

### 2. 合规路径：三条，按优先序

1. **改造**（首选）：改成契约形状（`edge_module_t` 生命周期 + 窄端口 + 事件）。
   代价是改代码，收益是它从此进质量门，且不再需要登记。
2. **包装**（次选）：保持原样，把它的接口包在**消费方定义的端口**后面（D14），适配器
   写在组合根。适用于"它是可复用的算法/驱动，但不认识我们的类型"。
3. **隔离为 `RAW`**（最后手段）：把该 target 排除在告警与静态检查之外，**并且登记**。
   适用于第三方源码（例：`pal/rtos/freertos`）。

路径 3 的规矩是硬的：

- `ci/exemptions.json` 必须有条目，字段缺一不可：`target` / `where` / `owner` /
  `reason` / `exit` / `issue`；`exit` 是**什么条件下拿掉这个豁免**。
- `.github/scripts/check_exemptions.py` **双向**强制：未登记的 `RAW` target 失败；
  登记了但那个目录不再声明 `RAW` 也失败（装饰性豁免比没有豁免更坏，它会掩盖下一个）。
- 豁免不是"以后再说"：没有 `exit` 条件的豁免在评审里视为未完成。

### 3. 它必须过哪些门禁、被豁免哪些

逐条列出。**被豁免的必须给出理由**，理由必须落到"这是第三方代码"或"这是预编译库"这
一类，而不是"改起来麻烦"。附录是门禁清单与允许的补救。

### 4. 退出条件

什么问题下这个单元会被改造、被移除、或被替换成合规实现。它会进 `ci/exemptions.json`
的 `exit` 字段，或者（走路径 1/2 时）进 PR 描述里的"后续"一节。

## 阻塞与时间模型 policy（最硬的一条）

**runner 是单任务协作式的**（D47）：`edge_sys_run_once()` 跑一轮就返回，产品循环调用它。
在这个上下文里做阻塞等待，会同时破坏两件事：调度（其它模块轮不到）与低功耗（本该
睡觉的时间在忙等）。

**只允许两种容器**：

1. **给它一个自己的 RTOS 任务**。此时它的阻塞是合法的，但**优先级映射必须写下来**，
   并且遵守 [`../rtos-runner.md`](../rtos-runner.md) §4 的规则：模块的 `priority` 是
   runner **内部顺序**，**不得**被当作任务优先级映射过去。这个任务与 runner 的关系
   （谁高谁低、共享什么、用什么同步）要写进 PR。
2. **改造成状态机**：把等待拆成"一步"，每轮由 runner 推进一次。适用于它能被切开的情况
   （多数协议解析可以，`delay()` 密集的裸算法通常不行）。

**禁止**：在 runner 内阻塞 —— 包括 `vTaskDelay`、忙等、等信号量、`HAL_Delay`。
理由：会破坏上面两件事，且在 tickless 打开后**连时间基准都不再线性**
（见 [`../flake-ledger.md`](../flake-ledger.md) 那条 tick 窗口记录）。

## 附录：门禁会拦你什么，以及允许的补救

| 门禁 | 症状 | 允许的补救 |
|---|---|---|
| `check_no_dynamic_memory.py` | 镜像里出现 `malloc` 家族符号 | 改成调用方提供存储；不要"就这一次"地放行（D21） |
| `check_stack_usage.py` | 某函数静态帧 > 512 B | 装进 `product/`（组合根按 D21 拥有调用方存储，门禁排除 `/product/`）**或**拆分函数；不许进 app/infra 后调高阈值 |
| `check_event_payload.py` | 事件里出现指针/变长字段 | 事件只能是标量事实（D16–D18）；要传载荷得走 D66 的通路（**尚未实现**，见下） |
| `check_layer_dependencies.py` | 依赖方向违规（如 `infra -> soc`） | 经**端口**（D14）+ 组合根适配器到达下层；**没有例外可开** |
| `check_app_isolation.py` / `check_app_transitive_includes.py` | app 直接 include infra/RTOS/寄存器/重量级 libc 头 | 把依赖改成 app 自己定义的接口，适配器进组合根 |
| `check_module_contract.py` | 模块带 `init`/`deinit` 进结构体，或回调不合法 | `init`/`deinit` 由组合根调用（D51） |
| `check_cmake_apps.py` / `check_area_registration.py` | CMake 清单与 `main()` 不一致、目录没有自声明 target | 按 D7/D88 补齐；两处清单必须一致 |
| `check_map_budget.py` / `report_size_trend.py` | 某层体积超预算或相对基线增长 | 先问"它是不是放错层了"；确实该涨就**显式更新基线**并写明理由 |
| `check_module_ids.py` / `check_event_ids.py` / `check_error_ids.py` | 新 ID 与中央表冲突或不在号段 | 在中央表里申请号段（D31/D55/D68），不在本地乱编 |
| `check_exemptions.py` | `RAW` 未登记 / 登记已过期 | 见路径 3 |

## 还没准备好的部分（诚实清单）

搬真实代码之前，下面这些**必须知道它们还没解决**：

- **预编译库（`.a`/`.lib`）没有落点与溯源规定**（[#149](https://github.com/cautonwong/modular/issues/149)）。
- **指针载荷没有通路**：事件是纯标量，而 D66（token + 经端口读载荷）在
  [`../adr-conformance.md`](../adr-conformance.md) 里仍是 ❌。传指针的既有接口**无法**
  变成事件 —— 这是 legacy 接入的结构性阻塞（[#147](https://github.com/cautonwong/modular/issues/147)）。
- **时间基准没有统一 policy**：`HAL_GetTick()`/`millis()`/`delay_ms` 如何映射到 PAL
  tick、RTOS tick、以及 tickless 之后的时间线，尚未成文。
- **三处既有规则互相矛盾**（厂商 HAL 落点 / 向量表归属 / `board_irq_attach`）：
  [#150](https://github.com/cautonwong/modular/issues/150)。

## 完成判据

- `python3 .github/scripts/check_exemptions.py` 通过（新豁免已登记，没有过期条目）。
- `python3 tests/guards/run_guard_selftest.py` 通过。
- 该单元必须过的门禁全绿；被豁免的逐条写在 PR 里，且理由成立。
- 走路径 3 时：`ci/exemptions.json` 条目六个字段齐全，`exit` 写的是**可判定的条件**。
- 走路径 1/2 时：它进了质量门（不再需要登记），并且有一个 host 测试覆盖它的契约面。
