# 并行开工 Runbook：串行文件清单、锁机制与冲突防御指南 (Issue #119)

> **适用场景**：本工程进入“8层架构骨架已立，多团队/多 Agent 并行填充业务应用、底层驱动、板卡与产品组合根”阶段。本 Runbook 明确界定**串行敏感文件**与**并行安全开发区**，提供解耦锁机制与冲突预防指南，确保百人或多智能体并发开发时零踩踏、零代码冲突、架构门禁持续绿灯。

---

## 1. 架构文件治理分类

```
                   ┌───────────────────────────────────────────────┐
                   │ 1. 串行受控区 (Serial Architecture Core)        │
                   │ (edge_module, CI/Toolchain, 中央 ID 注册表)   │
                   └───────────────────────┬───────────────────────┘
                                           │  只读依赖 / 独立治理 PR
                                           ▼
┌──────────────────────────────────────────────────────────────────────────────────┐
│ 2. 并行安全开发区 (Parallel-Safe Component Zones)                                 │
│                                                                                  │
│  app/<name>/         infra/<name>/       soc/<name>/        board/<name>/       │
│  业务状态机           外设纯驱动          芯片寄存器包        板级中断/适配器     │
│  (自声明 CMake)      (自声明 CMake)     (自声明 CMake)     (自声明 CMake)      │
└──────────────────────────────────────────────────────────────────────────────────┘
                                           │
                                           ▼
                   ┌───────────────────────────────────────────────┐
                   │ 3. 组合根装配区 (Product Composition Roots)     │
                   │ product/<name>/main.c & glue.c                │
                   │ (独占组装，严禁跨产品共享 glue)                │
                   └───────────────────────────────────────────────┘
```

---

## 2. 串行文件清单 (Strictly Serial / Governed Files)

以下文件为整套工程的“宪法骨架”与公共注册表面。**严禁在具体业务特性、驱动开发或日常 Bugfix 的 PR 中顺带修改**。对其任何修改必须单独提 Issue / RFC，并通过架构委员会评审后走独立 PR：

| 串行受控文件 / 路径 | 核心责任 | 为什么禁止并行混入修改 |
| :--- | :--- | :--- |
| `ci/toolchain.json`<br>`ci/adr-gates.json`<br>`.github/workflows/ci.yml` | 统一工具链基准版本、CI 自动化门禁矩阵、流水线规则 | 属于基础设施层，任意改动影响所有开发者构建与 CI 结果。 |
| `edge_module/include/edge/*`<br>`edge_module/src/*` | 基础运行时、事件队列（`edge_event_t`）、时钟契约、模块生命周期定义 | 全仓所有层级直接或间接依赖的基石，改动会引发全仓重编译与契约漂移。 |
| `edge_module/include/edge/modules.h`<br>`edge_module/include/edge/events.h`<br>`edge_module/include/edge/errors.h` | 全局 Module ID、Event ID、Error ID 中央注册表 | 避免多个开发者在同一枚举中分配相同 ID 产生编号冲突或踩踏。需先在 Issue 中确认号段分配。 |
| `CMakeLists.txt` (顶层) | 顶层基础构建指令、宏函数（`edge_add_*`） | 必须保持干净，仅维护全局编译标志与产品矩阵声明。 |
| `.github/scripts/check_*.py` | 29 项架构守卫脚本（代码宪章执行器） | 门禁规则升级须有完备的正反向测试用例与 ADR 记录。 |

---

## 3. 并行安全开发区 (Parallel-Safe Working Zones)

为避免开发者在同一文件中工作，工程遵循 **“目录自治与自声明 Target（ADR D88）”** 机制。每个组件均位于专属子目录下，自包含 `CMakeLists.txt`，由顶层 CMake 自动发现：

| 组件层级 | 规范目录路径 | 生成方式 (Scaffold 命令) | 内部文件规范 |
| :--- | :--- | :--- | :--- |
| **App** | `app/<name>/` | `python3 tools/scaffold.py app <name>` | • `CMakeLists.txt` (`edge_add_app`)<br>• `include/<name>/ports.h` (消费接口)<br>• `include/<name>/<name>.h` (聚合根)<br>• `src/<name>.c` (纯状态机) |
| **Infra** | `infra/<name>/` | `python3 tools/scaffold.py infra <name>` | • `CMakeLists.txt` (`edge_add_infra`)<br>• `include/<name>/<name>.h`<br>• `src/<name>.c` (驱动实现) |
| **SoC** | `soc/<name>/` | `python3 tools/scaffold.py soc <name>` | • `CMakeLists.txt` (`edge_add_soc`)<br>• `include/soc_<name>/soc_<name>.h`<br>• `src/soc_<name>.c` (厂商硬件沙盒) |
| **Board** | `board/<name>/` | `python3 tools/scaffold.py board <name> --soc <soc>` | • `CMakeLists.txt` (`edge_add_board`)<br>• `include/<name>/board.h`<br>• `src/board.c` (中断分发与静态分配) |
| **Product** | `product/<name>/` | `python3 tools/scaffold.py product <name> --board <b>` | • `main.c` (静态分配与生命周期起停)<br>• `glue.h` & `glue.c` (单向接口适配器) |

---

## 4. 冲突防御与无锁解耦机制 (Lock Avoidance Mechanisms)

### 4.1 接口首发权原则 (Consumer-Defined Ports - D14)
- **规则**：App 工程师对所需依赖拥有 100% 接口定义权，定义在 `include/<app>/ports.h` 中。
- **解耦效果**：
  - App 工程师不需要等待底层硬件驱动或存储驱动写完，即可先行编写业务逻辑与单元测试。
  - Infra 驱动工程师仅需编写通用硬件驱动，无需感知任何 App 细节。
  - 两者头文件物理隔离，彼此不存在 `#include` 交叉，从根本上杜绝跨层 Git Conflict。

### 4.2 厂商 SDK 沙盒隔离原则 (Vendor SDK Sandbox - Issue #149)
- **规则**：芯片厂商 SDK（STM32 HAL、Nordic nrfx、Pico SDK、CMSIS 等）只能出现在 `soc/<soc>/` 和 `pal/<pal>/`。
- **解耦效果**：
  - 厂商 SDK 的宏污染、非标类型（如 `u8`、`HAL_StatusTypeDef`）决不能溢出到公共接口。
  - 避免因不同芯片厂商 SDK 升级导致的整仓头文件依赖链崩溃。
  - 预编译二进制库（`.a`/`.lib`）只能由 `soc` 或 `pal` 目标私有链接，业务层完全零感知。

### 4.3 组合根独占原则 (Isolated Composition Roots)
- **规则**：每个产品独立享有 `product/<name>/` 目录。
- **解耦效果**：
  - 两个产品即使使用相同的 App 和 Board，也严禁共享 `glue.c`。
  - 组合根装配修改完全局限在各自的 `product/<name>/main.c` 和 `glue.c`，互不影响。

---

## 5. 本地开工与提交流水线 Checklist

开发者在本地分支开工并准备提交 PR 时，执行以下标准四步检查：

```bash
# 1. 骨架生成（可选：新组件开发）
python3 tools/scaffold.py app my_feature

# 2. 本地快速门禁自测（验证 29 项架构守卫规则与骨架生成器）
python3 tests/guards/run_guard_selftest.py
python3 tests/guards/run_generator_selftest.py
python3 .github/scripts/check_sdk_isolation.py .

# 3. 本地全量测试套件验证
cmake -S . -B build -G Ninja -DEDGE_MODULE_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# 4. Git 变更集洁净度自检（确保未意外修改串行文件）
git status -s
git diff --name-only main...HEAD
```
