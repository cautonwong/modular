# 轴一：pigweed facade/backend 模式 vs 我们的消费者定义端口

## 0. 结论先行

1. pigweed 的 facade 与我们 D14 的「消费者定义端口」解决的是**不同问题**：facade 是*编译期择一实现*
   （`pw_chrono_BACKEND` 一个构建只能有一个），端口是*构造期注入*。pigweed 自己说（`docs/sphinx/facades.rst:145-149`）：
   能负担依赖注入就**不要**用 facade；我们不需要 header 替换 → 整套机制照搬是负价值。
2. 值得借的是 facade 的**记账与文档纪律**：契约清单写在 public 头里、每个 facade 维护一份
   `backends.rst`、模块元数据是一张被 JSON schema 校验的表；三样都打在我们「无声明式依赖表、
   契约靠脚本守」的痛点上，且已有先例可循（`ci/dependencies.json`）。
3. 不值得借的是它的**构建期机制**：`public_overrides/` 头覆盖（pigweed 自认是 "things hiding under
   rocks"，`module_structure.rst:172-173`）、config header 的 `-include` 覆盖、`.facade` 循环依赖拆解
   （`pw_build/pw_facade.bzl:40-70`）——都是 C++/GN/Bazel 依赖图的产物。

## 1. 读过的路径

pigweed：`docs/sphinx/facades.rst`、`docs/sphinx/module_structure.rst`(132-215,516-600)、
`docs/sphinx/style/cpp.rst`(995-1010)、`docs/sphinx/module_metadata.json`+`_schema.json`+
`_extensions/module_metadata.py`、`pw_build/pigweed.cmake`(690-780)、`pw_build/facade.gni`(78-145)、
`pw_build/pw_facade.bzl`、`pw_assert/public/pw_assert/check.h`(110-130)+其 `*_public_overrides/`、
`pw_log/public/pw_log/log.h`(26-84)、`pw_log_string/public_overrides/pw_log_backend/log_backend.h`、
`pw_sys_io/public/pw_sys_io/sys_io.h`(38-130)、`pw_chrono/public/pw_chrono/system_clock.h`(21-51)、
`pw_chrono/{CMakeLists.txt,BUILD.gn,BUILD.bazel,backends.rst}`、`pw_chrono/system_clock_facade_test{,_c}.{cc,c}`、
`pw_thread/BUILD.gn`(277-345)、`pw_thread_stl/BUILD.gn`(174-183)、19 个 `pw_*/backends.rst`。

modular：`edge_module/include/edge/{module.h,ports.h}`、
`app/vesc_comm/include/vesc_comm/vesc_comm.h`(260-420)、`app/motor_config/include/motor_config/motor_config.h`(28-72)、
`app/motor_config/src/motor_config_internal.h`(41-43)、`.github/scripts/check_{consumer_ports,module_contract}.py`、
`tests/contract/port_contract.h`、`tests/CMakeLists.txt`、`tests/test_contract.c`、`ci/dependencies.json`、
`check_dependencies.py`、`docs/dependencies.md`、`docs/adr-conformance.md`、`docs/agents/domain.md`、
`docs/how-to/add-driver.md`。

## 2. pigweed 那边的事实

| 问题 | 事实 | 证据 |
|---|---|---|
| facade 是什么 | 「必须编译期满足的 API 契约」，backend 是其实现；命名 `{facade}_{backend}` | `docs/sphinx/facades.rst:19-27` |
| public 头如何声明 | public 头直接 `#include "pw_x_backend/x_backend.h"`，并把后端**必须提供的宏/符号逐条列在注释里** | `pw_log/public/pw_log/log.h:30-66`；`pw_assert/public/pw_assert/check.h:112-130` |
| 职责切分怎么写 | 逐条声明标注由谁实现：`@pre This function must be implemented by the pw_sys_io backend` | `pw_sys_io/public/pw_sys_io/sys_io.h:55,64,79,87` |
| 后端要提供什么常量 | 注释列出后端必须定义的编译期常量与函数 | `pw_chrono/public/pw_chrono/system_clock.h:24-31,51` |
| 后端如何被选中 | 构建变量：Bazel `label_flag`、CMake/GN `pw_x_BACKEND` | `facades.rst:44-49`；`pw_chrono/CMakeLists.txt:29-34`；`pw_chrono/BUILD.bazel:71-79` |
| 没选后端怎么报错 | 生成一个专用错误目标，报错文本里带**确切的修复变量名和示例行** | `pw_build/pigweed.cmake:714-724`；`pw_build/facade.gni:110-142` |
| `public_overrides` | 后端把契约头放在 `*_public_overrides/<facade>_backend/`，靠 include 路径顺序生效 | `facades.rst:41-43`；`module_structure.rst:170-202` |
| 契约怎么测 | facade 内有一条 `*_facade_test`（`enable_if = pw_x_BACKEND != ""`），并有一个 **C 文件**只调 C API；后端模块用一行 `deps` 复用同一套件 | `pw_chrono/BUILD.gn:93-104`；`pw_chrono/system_clock_facade_test_c.c:11-14`；`pw_thread/BUILD.gn:319-325` + `pw_thread_stl/BUILD.gn:174-183` |
| 契约可测试性 | facade 逻辑被拆到 impl 头，注释明说「为了不经过 facade/backend 构建设施直接测」 | `pw_assert/public/pw_assert/check.h:110-114` |
| 文档要求 | 每个 facade 模块有 `backends.rst` 列它的全部后端（19 个模块如此） | `pw_chrono/backends.rst`、`pw_log/backends.rst` |
| 声明式元数据 | `docs/sphinx/module_metadata.json` 声明 status/languages/tagline/size，由 JSON schema + `jsonschema.validate` 在文档构建时校验 | `module_metadata_schema.json:1-60`；`_extensions/module_metadata.py:78-90` |
| 命名风格 | C 符号必须带模块名前缀；「facade 的后端可以用 facade 名做前缀」 | `docs/sphinx/style/cpp.rst:1000-1005` |
| 副作用拆解 | 后端把重依赖放进 `backend.impl`，避免 include/依赖环 | `pw_log/docs.rst:332-363` |
| 官方警告 | 「模块只能有一个后端、无法运行期替换、测试更难——能用虚接口/回调/模板就不要用 facade」 | `module_structure.rst:533-545`；`facades.rst:132-149` |

## 3. 我们这边的对应物（现状）

- 端口即契约、消费者定义：`edge/ports.h:18-56`（规范形状 + `void *self`）；`vesc_comm.h:260-420`
  五个端口；`motor_config.h:28-33` 变量存储端口。
- 不透明类型 + 调用方内存：`motor_config.h:49-72`（`STORAGE_SIZE/ALIGN`），定义与断言在
  `motor_config/src/motor_config_internal.h:41-43`。
- 契约靠脚本守：`check_consumer_ports.py`、`check_module_contract.py`、`check_module_ids.py`（表驱动）。
- 契约靠测试固化：`tests/contract/port_contract.h` 写「形状不决定行为」的语义答案，实现者跑一遍；
  `tests/CMakeLists.txt:31-45` 用 CTest `WILL_FAIL` 证明套件**能**失败。
- 已有的声明式先例：`ci/dependencies.json` + `check_dependencies.py`（形状 + 与构建一致 + 不空）。

## 4. 值得借（按上面五类）

**契约写法（价值最高）**
1. 「谁实现这条」逐条写在声明处：`sys_io.h:55` 的 `@pre` 模式 → 我们 `ports.h`/app 端口上写
   `@impl 由产品 glue 提供`、`@impl 由 fake 提供`。我们的端口现在只写*形状*，没写*哪一侧实现*。
2. 后端必须提供的符号清单写成头文件里的**枚举块**（`check.h:116-130`）：对应我们
   「实现一个端口必须提供哪几个回调 + 语义」的清单，可直接放在端口 typedef 上方（我们
   `port_contract.h:11-23` 已有语义答案的先例，缺的是「必需成员清单」）。

**测试方式**
3. 契约套件 + 一行实例化（`pw_thread/BUILD.gn:319-325` + `pw_thread_stl/BUILD.gn:174-183`）：
   我们已经是这个形状（`edge_test_contract` 静态库 + `edge_add_host_test(... DEPS edge_test_contract)`），
   差距只在「哪些实现者必须跑」没有清单——与提案 2 合并，不必改机制。

**文档要求**
4. facade 自己的 `backends.rst` 索引（`pw_chrono/backends.rst`）：在 `edge/ports.h` 顶部或
   `docs/architecture.md` 维护「端口 → 已知适配者」清单，由 `check_docs.py` 保证路径存在。
   注意与我们「`adr-conformance.md` 是唯一状态视图」的冲突：这是**导航**不是**决策状态**，
   落地时必须写清这条边界，否则违反 D 记录纪律。

**声明式表（价值最高）**
5. `module_metadata.json` + schema + `jsonschema.validate`（`_extensions/module_metadata.py:78-90`）：
   我们 `ci/*.json` 已是同一形态，缺的是「端口 → 适配者 → 测试」这张表。

**命名 / 头文件布局**
6. 命名：`{facade}_{backend}` 不适用；可用的是「后端符号可复用 facade 名前缀」(`style/cpp.rst:1000-1005`)
   → 我们继续用 `product/*/glue.c` 的 `<app>_<role>_` 前缀，不新造 `impl` 后缀。
7. 布局：`module_structure.rst:158-165` 的 `public/<m>/internal/` 与我们的 `include/<name>/` + `src/`（D36）
   等价，但它解决了「私有头要共享给另一个模块」的缺口——真有这种需求时再引入，不做预防性改造。

## 5. 不适合我们（及代价）

1. **整套 facade/backend 选择机制**：GN/CMake/Bazel 三套后端变量 + `label_flag` + `.facade` 子目标
   + `public_overrides`。代价：把「谁被装配」从组合根 `main()`（D6、D7）搬到构建系统里，
   等于编译期的 service locator，直接违反我们冻结的「组合根」原则；收益（header 替换）我们不需要。
2. **`public_overrides/` 头覆盖**：依赖 include 路径顺序，同名头能互相覆盖；pigweed 自己叫它
   "things hiding under rocks"（`module_structure.rst:172-173`）。我们
   `check_app_transitive_includes.py` + 单一路径更可读，照搬会破坏可发现性。
3. **config header 覆盖（`PW_FOO_CONFIG_HEADER` / `-include`）**：会让同一模块随构建改变形状，
   威胁我们 host 单测与固件同一套代码的前提；我们的等价物是 CMake 选项 + `ci/exemptions.json`。
4. **`backend.impl` 循环依赖拆解**（`pw_log/docs.rst:332-363`）：那是 C++ include 环 + 依赖图问题，
   我们的端口是纯 C 头 + 函数指针，不存在这个环。
5. **C++ 侧的零开销论证**（`facades.rst:124-127`）：常量内联/静态分配后端类型依赖 C++ 语义，C11
   拿不到；我们的静态分配已由「调用方提供内存 + `_Static_assert`」覆盖。
6. **C 可调用镜象测试**（`*_facade_test_c.c`）：我们本来就是 C11，无收益；我们的等价保证是
   `extern "C"` 头可被 C++ 消费，而仓库当前没有任何 `.cpp`（见 R3），不要去补一个不存在的需求。

## 6. 代价与风险汇总

| 借鉴项 | 代价 | 风险 |
|---|---|---|
| 契约注释（谁实现/必需成员） | 小：只改头注释，零代码 | 注释会漂移：无 gate 能验；要么接受，要么只写「必需成员」并由现有 `check_consumer_ports.py` 顺带校验成员数 |
| 端口适配者清单 | 中：新增一份 JSON + 校验 | 与正则扫描重叠成「两份真相」→ 必须让清单成为唯一来源，正则降级为形状校验 |
| `backends.rst` 式索引 | 小 | 与 `adr-conformance.md` 唯一状态视图的边界争议，需要一句话写死在文档里 |
| 断言消息里带修复方法 | 小 | 无 |

读到但未验证：R1 「每个 facade 都有 `backends.rst`」/「后端符号清单与实现一致」在 pigweed 里**没找到**
工具校验（文档构建只校验 `module_metadata.json`，`_extensions/module_metadata.py:78-90`），属人工纪律；
提案 1/3 里的 gate 要我们自建。R2 本报告未跑 pigweed 构建。R3 仓库无 C++ 消费方（无 `.cpp`），
`extern "C"` 头是空承诺，故 C 镜象测试不该借。

## 7. 排序提案（≤5）

1. **端口头补三段式契约注释：必需成员 / 谁实现 / 语义不变量** — 做什么：在 `edge/ports.h` 与 app
   公共端口的每个 typedef 上方加 `@impl`（product glue / fake）与「必填回调清单」；语义答案继续放
   `tests/contract/*.h` 不重复写。为什么适合：零代码、零构建影响，直接补上「端口靠 `_Static_assert`
   + 脚本守」的可读性缺口。工作量：**小**。风险：注释漂移；缓辙是在 `check_consumer_ports.py`
   里顺手校验「`@impl` 标注存在」。
2. **新增 `ci/ports.json`：端口 → 适配者 → 契约套件，成为唯一来源** — 做什么：照抄
   `ci/dependencies.json` 形态（`check_dependencies.py` 的「形状 + 与构建一致 + 不空」三条），
   把 `check_consumer_ports.py` 从「纯正则扫描」升级为「读清单 → 校验目标路径/套件注册存在」，
   正则只留 `void *self`、无 malloc 这些形状规则。为什么适合：正面命中「无声明式依赖表」的痛点，
   且仓库已有同一模式的成功先例，学习成本几乎为零。工作量：**中**（新增 1 份 JSON + 改 1 个脚本 +
   加一组 `tests/guards/` 反向夹具）。风险：清单与正则重叠；必须一次说清谁是唯一来源。
3. **`edge/ports.h` 顶部加「端口 → 已知适配者」索引，并在文档里划清它不是状态视图** —
   做什么：模仿 `pw_chrono/backends.rst`，每个规范端口列出适配者（product glue / infra fake），
   由 `check_docs.py` 保证链接不烂。为什么适合：新 agent/新同事找「谁实现了这个端口」现在要 grep；
   一行索引解决。工作量：**小**。风险：被误当第二份状态视图，与 D 记录纪律冲突；需在文件头写死边界。
4. **编译期断言的报错文本带上修复指令** — 做什么：学 `pw_build/facade.gni:122-140` 与
   `pigweed.cmake:714-724`，把 `_Static_assert`/构建期检查的消息改成「怎么修 + 改哪个文件」：
   `MOTOR_CONFIG_STORAGE_SIZE` 不匹配时直接给出重新测量的命令与文件路径。为什么适合：
   `motor_config_internal.h:41-43` 只说 “stale value"，读者得自己翻 `motor_config.h:49-72`；
   改字成本极低。工作量：**小**。风险：无。
5. **（可选）拒绝项留档：在 `docs/architecture.md` 记一条「为什么不做编译期 backend 选择」** —
   做什么：引用 pigweed 自述（`facades.rst:145-149`、`module_structure.rst:533-545`）与我们的
   D6/D14，写明 facade 是编译期 service locator，与我们冻结原则冲突，避免下一轮研究重复讨论。
   为什么适合：研究产出若不沉淀成决策就必然被重问。工作量：**小**。风险：与 `adr-conformance.md`
   的单一状态视图边界（应放 `docs/architecture.md` 而非新建状态表）。
