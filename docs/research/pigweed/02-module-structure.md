# 轴二：pigweed 模块内部结构与依赖治理 —— 可执行借鉴清单

> 只读研究，未改动 pigweed / modular 任何源码或配置。所有结论带文件路径（必要时行号）。

## 0. 读过的路径

- pigweed：`docs/sphinx/module_structure.rst`(614 行)、`docs/sphinx/facades.rst`、`docs/sphinx/build/overview.rst`；
  实地模块 `pw_assert/`、`pw_log/`、`pw_base64/`、`pw_presubmit/`、`pw_module/`（`py/pw_module/check.py`、`create.py`）；
  治理件 `pw_build/pigweed.cmake`(1015 行)、`pw_build/facade.gni`、`pw_build/pw_facade.bzl`、`PIGWEED_MODULES`、
  `pw_build/generated_pigweed_modules_lists.gni`、`pw_build/py/pw_build/generate_modules_lists.py`、
  `docs/sphinx/module_metadata.json` + `module_metadata_schema.json` + `docs/sphinx/_extensions/module_metadata.py`、
  `pw_build/py/pw_build/bazel_to_gn.py`。
- modular：`cmake/EdgeTargets.cmake`、根 `CMakeLists.txt`、`app|infra|sys|board|pal|soc/*/CMakeLists.txt`、
  `.github/scripts/check_layer_dependencies.py`、`check_module_contract.py`、`check_area_registration.py`、
  `check_cmake_apps.py`、`check_exemptions.py`、`check_dependencies.py`、`check_guard_coverage.py`、
  `tests/guards/`、`ci/*.json`、`docs/adr.md`、`docs/dependencies.md`。

## 1. pigweed 一个模块长什么样（实地）

约定在 `docs/sphinx/module_structure.rst:34-104` 的目录树里，关键划分：

| 位置 | 语义 | 实例 |
|---|---|---|
| `public/<module>/*.h` | 公开头，唯一入口 | `pw_base64/public/pw_base64/base64.h` |
| `public/<module>/internal/*.h` | 被迫暴露、但标注"不许用" | `pw_assert/public/pw_assert/internal/print_and_abort.h`（`pw_assert/BUILD.gn:141`） |
| `*_public_overrides/` | 覆盖别人的头必须显式分目录 | `pw_assert/assert_compatibility_public_overrides/pw_assert_backend/assert_backend.h` |
| 模块根 | 实现 `.cc` + `docs.rst` + `OWNERS` + 构建文件 | `pw_base64/base64.cc`、`pw_base64/docs.rst`、`pw_base64/OWNERS` |

规则落成脚本而不是口号：`pw module check`（`pw_module/py/pw_module/check.py`，5 条 PWCK）

- PWCK002 有 `.cc` 就必须有 `*test.cc`（`check.py:151`）；
- PWCK004 必须有 rst 文档（`check.py:169`）；
- PWCK005 有 C/C++ 就必须有 `public/<mod>/*.h` 或 `public_overrides/`（`check.py:175-199`）；
- 同一函数里还要求 `public/` 下**只有一个**目录，多了就提示"你大概想放 `public_overrides/`"（`check.py:201-206`）。

**自认只对新模块生效**：`module_structure.rst:27-31` 原文 "Many Pigweed modules do not currently conform…migrated over time"。实测也对不上：

- `pw_assert/docs.rst`、`pw_presubmit/docs.rst` 在模块根，不在树的 `docs/`；
- `pw_presubmit/` 只有 `BUILD.bazel` + `BUILD.gn`，**没有 CMakeLists.txt**（纯 Python 模块），而树里 `CMakeLists.txt` 是无条件项。

三套构建如何维持一致：**没有一致性检查，靠"生成器起步 + 人"**。

- 唯一"写一次出三份"的入口是 `pw module create`（`pw_module/docs.rst` 的 `--build-systems gn,bazel,cmake`）；
- 从 Bazel 生成 GN 的工具存在，但定位是给第三方/遗留：`pw_build/py/pw_build/bazel_to_gn.py:16` "Generates BUILD.gn files from rules in Bazel workspace"；
- 漂移是实锤：`pw_base64/base64.cc:19` include 了 `pw_assert/check.h`；Bazel 显式写 `implementation_deps = ["//pw_assert:check"]`（`pw_base64/BUILD.bazel:32`），而 `pw_base64/BUILD.gn:27-30` 与 `pw_base64/CMakeLists.txt:22-25` 都没写，靠传递依赖侥幸编译通过。

→ **结论：三套并存是 pigweed 的负债，不是我们的模板。我们只有一套 CMake，应把它当作优势。**

## 2. pigweed 在依赖/可见性上的"声明 + 构建器强制"

### (a) 依赖分三档（pigweed 比我们多的第一个真东西）

| 构建 | 私有边 | 可传导边 |
|---|---|---|
| Bazel | `implementation_deps`（`pw_base64/BUILD.bazel:32`） | `deps`（`:34-37`） |
| GN | `deps` | `public_deps`（`pw_base64/BUILD.gn:27`） |
| CMake | `PRIVATE_DEPS`（`pw_base64/CMakeLists.txt:33`，此处属测试） | `PUBLIC_DEPS`（`:22-25`） |

GN 还能把"只给自己用"的 config 锁死：`visibility = [ ":*" ]`（`pw_assert/BUILD.gn:31,46,51`）。
Bazel 用 `//:__subpackages__` 表达"API 未稳定，先只给内部"（`pw_build/BUILD.bazel:98-99` 注释 "Restrict API to Pigweed until the api stabilizes"）。

### (b) CMake 侧把依赖做成 configure 期可校验的（`pw_build/pigweed.cmake`）

- `pw_target_link_targets(NAME ${PUBLIC,PRIVATE})`（`:143-176`）：每个依赖**必须是 CMake target**；当场找不到就用
  `cmake_language(DEFER ...)` 推到 configure 末尾再查（`:159-171`），由 `_pw_target_link_targets_deferred_check` 报
  `"...'s dep \"X\" is not a target."`（`:189-193`）。→ 依赖名打错 = configure 失败，不是链接期的平台惊喜。
- 目标名 = 目录名：`_pw_check_name_is_relative_to_root`（`:469-502`）要求 `pw_add_library(pw_foo ...)` 的 target 名等于相对路径点分名，否则 `FATAL_ERROR`。

### (c) facade：可换依赖，且"没绑后端就必须构建失败"

- GN `pw_facade`（`pw_build/facade.gni:79-172`）：backend 为空时 `public_deps = [ ":$target_name.NO_BACKEND_SET" ]`（`:171-172`），
  指向一个 `pw_error`，消息是 "Attempted to build the $_label facade with no backend."（`:129-140`）。
- Bazel 用 `label_flag`，默认 `//pw_build:default_module_config`；GN 默认 `pw_build_DEFAULT_MODULE_CONFIG`（`module_structure.rst:281-330`）。
- 对我们的映射：端口绑定/RTOS 选择。我们的等价物更严 —— `edge_add_product` 在 configure 期直接 `FATAL_ERROR`（`CMakeLists.txt:98-135`）。
  **pigweed 多的只有"默认后端"这一档，而那一档会让未绑定静默通过 —— 不该借。**

### (d) 声明式元数据 + 双向校验（第二个真东西）

- 模块注册表 `PIGWEED_MODULES`（192 行纯清单）+ `generate_modules_lists.py`：三种模式 WARN/CHECK/UPDATE（`:197-200`）；
  `--mode=CHECK` 做两件事 —— 扫 `pw_*` 目录找出"有内容却不在清单里"的模块（`_missing_modules`, `:180-199`），
  以及清单未排序（`:97-110`）。挂在 GN 目标 `check_modules`（`BUILD.gn:197-204`）。
- 每模块一行元数据：`docs/sphinx/module_metadata.json`（192 条，含 `languages` / `status` / `tagline` / `size`），
  schema 把 `status` 限定为 `stable|unstable|experimental|deprecated`（`module_metadata_schema.json:36-45`），
  文档构建时 `jsonschema.validate(metadata, schema)`（`docs/sphinx/_extensions/module_metadata.py:84-90`）。

## 3. 我们这边现状（对照，均有文件依据）

- 形状唯一来源：`cmake/EdgeTargets.cmake`（120 行）；`edge_add_layer_module` 强制 DEPS 非空（`:40-43`）；D88/D27 见 `docs/adr.md:122` / `:61`。
- 依赖图：`check_layer_dependencies.py` —— 层矩阵 `ALLOWED`（`:25-33`），先查 include（`:131`）再查 CMake DEPS（`:144-147`），另有 app 跨模块 include 的额外禁令（`:134-140`）。
- 注册双向：`check_area_registration.py`（目录必自报、不得绕过 helper、注册键=目录名）；`check_cmake_apps.py`（CMake app 列表 vs `main()` 的 `*_construct`）。
- 豁免双向：`ci/exemptions.json` + `check_exemptions.py`（RAW 目标必须登记；登记了却不再 RAW 也算错）。
- 第三方依赖清单：`ci/dependencies.json` + `check_dependencies.py`（清单与 CMake pin 必须一致，不许两份记录打架）。
- 新守卫必须带正负夹具：`tests/guards/run_guard_selftest.py` 的 CASES + `check_guard_coverage.py`。

## 4. 差距：pigweed 在"依赖声明化"上比我们多什么

1. **依赖边分档（private / transitive）**。我们只有一档：`target_link_libraries(${target} PUBLIC ${M_DEPS})`（`cmake/EdgeTargets.cmake:64`；HEADERS 分支 `:54` INTERFACE）。
   → 我们的 DEPS 全是可传导的，A→B→C 会让 A 白拿 C 的 include；**明写在 DEPS 里的边受检，漏写的边在编译期不一定报错**（`check_layer_dependencies.py` 也只看层，不看层内模块→模块）。
2. **清单 ↔ 文件系统双向校验的模块注册表**。我们是发现式（D88），只有"目录必自报"，没有"这个模块进全局清单了吗"。
3. **每模块一行、schema 校验的元数据**（status/languages/size）。我们没有任何模块成熟度字段：新模块与稳定模块在机器眼里等价。
4. **模块结构 linter（PWCK）**：我们只有文档健康检查（`check_docs.py`），没有"每模块必须有 `include/<name>/<name>.h`、CMakeLists、文档、测试"的结构检查（部分被 `check_area_registration.py` 覆盖）。
5. **目标名校验时机**：我们是 CI 期，pigweed 是 configure 期。

## 5. 明确"不适合我们"的部分（附代价）

| 不适合 | 代价/风险 |
|---|---|
| 三套构建并存（Bazel/GN/CMake） | C++17+Bazel 生态的负债；我们一套 CMake 是优势。不借，建议写进 ADR |
| 全局模块清单 `PIGWEED_MODULES` 那种"再记一份谁存在" | 与 D88（目录自报 + 根文件只发现）方向相反，两份记录必然漂移；除非它同时是 CI 矩阵/文档索引的唯一输入 |
| facade 的默认后端（`default_module_config`） | 默认空实现让未绑定静默通过；我们 configure 期 FATAL_ERROR 更严 |
| PWCK "只管新模块"的宽容 | 我们没有新旧之分，任何新守卫立刻对 40+ 存量模块生效；不配 `ci/` 豁免表就会变成噪音或被绕过 |
| 每模块一个 JSON 文件 | 放各模块=分散且需双向校验；放 `ci/`=与 CMakeLists 的 DEPS 构成双记录。取舍见 P3/P4 |

## 6. 提案（按性价比排序）

**P1. 依赖边分档：`DEPS` 默认私有，另开 `PUBLIC_DEPS`（先做，收益最大）**
- 做什么：`edge_add_layer_module` 增加 `PUBLIC_DEPS` 多值参数；`DEPS` 改 `target_link_libraries(... PRIVATE ...)`；只有"公开头 include 了它"的边才写 `PUBLIC_DEPS`；`check_layer_dependencies.py` 同时认两份声明（层矩阵对两者都生效）。
- 为什么适合我们：D27 的"精确 include 隔离"正被 PUBLIC 传染破坏；改完 DEPS 就是**真**边表，漏登记立刻变编译错误，且不需要任何新元数据。
- 工作量：小（`cmake/EdgeTargets.cmake` ~10 行 + 检查脚本 ~20 行 + `tests/guards` 正负夹具 + CASES 两条 ≈ 半天）。
- 风险：中。存量模块会真实爆出漏登记的边；先给检查脚本加一个 `--report` 模式统计爆炸半径，再按 area 分批切。

**P2. 目标名 = 目录名，从 CI 期提前到 configure 期**
- 做什么：在 `edge_add_layer_module` 里取 `CMAKE_CURRENT_SOURCE_DIR` 的目录名，与 `name` 不等就 `FATAL_ERROR`；沿用 `check_area_registration.py` 对 `pal` 的既有豁免。
- 为什么适合我们：等价于 `pw_build/pigweed.cmake:469-502` 的思想，但只在 CI 守（`check_area_registration.py`）；提前到 configure 期，本地第一次 build 就拦住，成本约 3 行。
- 工作量：小（约 30 分钟，含一条负向夹具）。
- 风险：低。注意 `pal/rtos/freertos` 目录名≠注册名的既有特例。

**P3. 模块级可见性白名单 `ci/module-visibility.json`（pigweed 用 GN visibility / Bazel `//:__subpackages__` 表达的那一层）**
- 做什么：新清单声明"受限模块 → 允许依赖它的模块/层"（如 `soc_mps2` 只允许 `board/mps2`、`pal_os` 只允许 `pal/*`）；`check_layer_dependencies.py` 增加一条规则：DEPS/include 中出现的受限模块必须命中白名单。双向：白名单里没人再依赖的条目报"过期"。
- 为什么适合我们：这是把我们唯一缺的"声明式依赖表"做成**模块级可见性**，形状完全照 `ci/exemptions.json` + `check_exemptions.py` 的既有做法（同一份仓库里已有先例，评审语言统一）。与 D7（拒绝**产品**manifest）不冲突：D7 管产品组成，这条管模块可见性。
- 工作量：中（脚本 + 正负夹具 + 首版白名单盘点 ≈ 1-2 天）。
- 风险：低-中。首版白名单要如实反映现状（否则一上线就红）；要明确"白名单不是 D7 的 manifest"。

**P4. 每模块一行元数据 `ci/modules.json`（+ schema），且必须有第二个消费者**
- 做什么：字段取 pigweed 的最小集：`layer` / `owner` / `status(stable|experimental|porting)` / `docs` / `tests_required`；照 `PIGWEED_MODULES` 的双向校验写"目录↔条目"（`generate_modules_lists.py:180-199` 思路），照 `module_metadata_schema.json` 写 schema，照 `ci/dependencies.json` 的写法给 `note`。
- 为什么适合我们：**覆盖率门禁 95% 实测 93% 的豁免、以及"哪些模块必须带 host 测试"目前无处声明**；这份表可以同时喂 CI 矩阵、覆盖率豁免和文档索引，避免"纯登记表"。
- 工作量：中（schema + 校验 + 扫目录 + 双向夹具 ≈ 1 天）。
- 风险：中。必须落实至少两个真实消费者，否则几个月后变成第二份过期清单；`status` 的取值要先和 ADR 对齐。

**P5. 把"不借清单"写进 ADR（一条），封住照搬冲动**
- 做什么：`docs/adr.md` 增一条：不引入第二套构建系统、不引入全局模块存在清单（D88 已定发现式）、不引入"默认后端/默认空实现"、任何新结构守卫必须配 `ci/` 豁免表与正负夹具；`docs/adr-conformance.md` 记一行状态。
- 为什么适合我们：这份报告最大的用途是**防止下一个人把 C++17/Bazel 假设搬进来**；成本一条 ADR，收益是后续每次评审都不再重复争论。
- 工作量：小（1 小时）。
- 风险：低。仅需注意与 D7/D88/D27 的引用不要互相矛盾。
