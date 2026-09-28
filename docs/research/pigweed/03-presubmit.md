# 轴三：presubmit / 质量门禁 —— pigweed 可借鉴清单

> 只读调研。证据均为 pigweed 仓库内路径+行号；本报告不改动任何源码/配置。

## 0. 读到的关键路径

pigweed：`pw_presubmit/py/pw_presubmit/pigweed_presubmit.py`（PROGRAMS:1316）、
`private/upstream_programs.py`（QUICK_COMMON:56 / FULL_COMMON:75）、`presubmit.py`（filter_paths:465）、
`cli.py`、`format_code.py`（CODE_FORMATS:598）、`format/step.py`（CodeFormatting:26）、
`cpp_checks.py`、`upstream_checks.py`、`owners_checks.py`、`keep_sorted.py`、`todo_check.py`、
`inclusive_language.py`、`block_submission.py`、`json_check.py`、`shell_checks.py`、`source_in_build.py`、`private/bazel.py`。

modular：`.github/scripts/check_*.py`（22 个，其中 pre-commit 挂 16 个）、`.github/workflows/ci.yml`、
`ci/{adr-gates,dependencies,exemptions,size-budget,size-baseline,toolchain}.json`、`.clang-tidy`、`.pi-lens.json`、`.githooks/pre-commit`。

## 1. pigweed 实际跑的步骤集合

程序划分在 `pigweed_presubmit.py:1316`，共 10 个 program：`quick / lintformat / full / sanitizers / security /
fuzz / internal / sapphire / arduino_pico / other_checks`。默认 program 是 `quick`（`pigweed_presubmit.py:1334`）。

**A. 轻量通用检查（`QUICK_COMMON` `private/upstream_programs.py:56`）**
| 步骤 | 作用 | 证据 |
|---|---|---|
| `copyright_notice` | 每文件 13 行 Apache 头，含错字检测，**可自动修** | `upstream_checks.py:506`、`:408` |
| `inclusive_language_check` | 词表（master/sanity/dummy/he-his…），带 ignore/enable 标签 | `upstream_checks.py:726`、`inclusive_language.py:29` |
| `block_submission` | `DO NOT SUBMIT` 等阻塞短语 | `block_submission.py:10`、`:26` |
| `pragma_once` | 每个头文件必须有 `#pragma once`，排除 `*.pb.h` | `cpp_checks.py:47` |
| `owners_lint_checks` | OWNERS 语法 + 依赖递归校验 | `upstream_checks.py:532`、`owners_checks.py:27` |
| `source_in_gn_build` | 源文件必须出现在 GN 构建里 | `upstream_checks.py:692`、`source_in_build.py:33` |
| `json_check` | 所有 `.json` 可解析 | `json_check.py:20` |
| `keep_sorted` | `keep-sorted: begin/end` 区间真的有序 | `keep_sorted.py:38` |
| `todo_check` | TODO 必须带 bug 号或用户名 | `upstream_checks.py:687`、`todo_check.py:14` |

**B. 格式化（`format_code.py:598` CODE_FORMATS，每语言一个 step `format/step.py:26`）**
C/C++(clang-format)、Proto、Java、JS/TS(prettier)、Go、Python(black)、GN、Bazel(buildifier)、CMake、
RST、Rust、Markdown、OWNERS、JSON、trailing-space。每个都 check+**fix** 双模态。

**C. 重检查（`FULL_COMMON:75` / `OTHER_CHECKS:1174` / `SANITIZERS:1236`）**
受影响目标增量构建+测试、mypy+pylint、Bazel aspect 跑 clang-tidy（`private/bazel.py:188`）、GN/ninja 全量构建、
sanitizer 矩阵（asan/tsan/ubsan/msan，`cpp_checks.py:241`）、coverage、docs 构建、cmake gcc/clang 双构建、
bazel lockfile 校验、gitmodules 禁止子模块、shellcheck、source-in-cmake/soong、module OWNERS 强制、
stm32f429i/zephyr/rp2040/rp2350 交叉构建、python 约束与 PyPI 版本检查。

## 2. 粒度与成本（pigweed 的设计核心，不是步骤清单）

- **按改动文件过滤**：每个 step 声明 `file_filter`/`endswith`，运行时只喂改动路径 ——
  `presubmit.py:132` `program.filter(self._root, self._paths)`，日志打 "N of M checks apply"（`presubmit.py:145`）。
- **可修复而非仅报错**：`@step(fix=...)`（`upstream_checks.py:506`），`pw format --fix`（`format/step.py:51`）。
- **可用性开关**：`-k/--keep-going`、`--dry-run`、`--output-directory`、`--install`（装 pre-push hook，
  `pigweed_presubmit.py:1330`）、`--only-list-steps`（给 CI/MILO 拆步，`cli.py:232`）。
- **每步独立日志**：`step.log` + `failure-summary.log`（`presubmit.py:213`）。

成本量级：A 组全是纯文本/正则，毫秒级、零外部依赖；B 组依赖格式化工具二进制；C 组是分钟级、需要完整工具链。
**性价比全在 A+B**，C 组对我们是幻觉收益（见 §6）。

## 3. 我们的现状（对照）

- **22 个 `check_*.py`**，pre-commit 挂 16 个（`.githooks/pre-commit` 的 `SOURCE_GUARDS`），
  架构 job 跑 16 个 + guard 自测 + `check_guard_coverage.py`（`ci.yml` architecture:16 个 step）。
- **`ci.yml` 8 个 job**：`changes`（docs-only 快路径）→ `build-test`（gcc/clang × debug/release + asan/ubsan，
  6 组合）→ `coverage`（gcovr `--fail-under-line 95`）→ `product-matrix`（5 产品 + 7 个负例 configure 必须失败）
  → `neutrality` → `cross-compile-arm` → `static-analysis` → `architecture` → `ci-success` 汇总。
- **`static-analysis` job**：clang-format `--dry-run --Werror`、clang-tidy 硬编码 18 个文件、cppcheck。
- `ci/exemptions.json`：每个 RAW 目标带 owner/reason/exit/issue（`check_exemptions.py` 守）。
- `ci/adr-gates.json`：ADR→gate 矩阵 + `must_gate` 白名单（`check_adr_gates.py`）。
- `.clang-tidy`：显式 check 集 + `WarningsAsErrors: '*'`。`.pi-lens.json`：规则禁用含 reason/exit。

## 4. 我们缺的步骤（按性价比排序）

1. **`DO NOT SUBMIT` / 冲突标记阻塞检查** —— 我们完全没有。纯正则、零依赖、防的是最贵的事故（误合并）。
   借 `block_submission.py:10`，可直接并入 `architecture` job。
2. **许可证/版权头检查** —— **我们连 `LICENSE` 文件都没有**（`ls LICENSE*` 无输出；`CONTRIBUTING.md`/`README.md`
   无 license/copyright/spdx 字样）。pigweed 用 13 行头守（`upstream_checks.py:408`）。
   代价：必须先有**产品/法务决策**（选什么许可证），这是决策不是工程。见 §7 提案 2。
3. **`#pragma once` / include-guard 检查** —— 我们有 56 个 `__cplusplus` 头，但无守卫。`cpp_checks.py:47` 约 15 行可抄。
   代价：需先确认现有头文件**已经全部合规**，否则一次性要改一堆文件（先跑一次 dry-run 看数量）。
4. **TODO 必须带 issue 号** —— 我们有 `todo.md`/`.proposal.md`，无 TODO 格式门禁。`todo_check.py:14`。
   代价：需定 issue 前缀规则（我们的 tracker 是 `cautonwong/modular`，`docs/agents/issue-tracker.md`）。
5. **`keep_sorted` 区域检查** —— 我们的 `ci/adr-gates.json` 的 gate 列表、`.clang-tidy` 的 check 列表、
   `ci.yml` 的 matrix 都是手排列表，会漂。`keep_sorted.py:38` 500 行，重；
   但只抄"`keep-sorted: begin/end` 区间断言有序"这一半约 40 行即可。
6. **guard 脚本自身的静态检查（mypy/pylint/ruff）** —— pigweed 对 Python 跑 mypy+pylint（`private/bazel.py:162`）。
   我们 22 个守卫脚本**无类型、无 lint、无 CI**；仓库里有 `.ruff_cache/` 但无 ruff 配置也无 CI step。
   这是"守门的门没人守"。代价：低（装 ruff，加一个 job），风险：会先冒出若干告警。
7. **每步独立日志/失败摘要 + `--keep-going`** —— 我们的 pre-commit 只往 stderr 打，CI 靠 30+ 个 step 名定位。
   中等收益，中等工作量（要改 22 个脚本的统一输出约定）。

## 5. 我们比它强的地方（pigweed 该学我们）

1. **固件体积门禁**：pigweed presubmit **没有任何 size/bloat 门禁**——`pw_bloat/` 只是报告库，
   `PROGRAMS` 里唯一的引用是 rp2xxx 通配构建的 `//pw_bloat:bloat_base`（`pigweed_presubmit.py:988`）。
   我们有三层：ELF flash/RAM（`check_size.py`）、按层 map 预算（`check_map_budget.py` + `ci/size-budget.json`）、
   趋势对比基线（`report_size_trend.py` + `ci/size-baseline.json`），外加静态栈深（`check_stack_usage.py`）
   与零动态内存（`check_no_dynamic_memory.py`）。
2. **可复现构建 + 供应链**：`ci.yml` cross-compile-arm 里 `SOURCE_DATE_EPOCH` + 两次构建 `cmp` +
   `generate_build_metadata.py` / `generate_sbom.py`。pigweed 无对应 presubmit 步骤。
3. **守卫的"反证"元门禁**：`check_guard_coverage.py` 强制每个 `check_*.py` 同时有
   `expect_pass=True` 和 `False` fixture（`tests/guards/`）。pigweed 有模块单测，但**没有**"每个 presubmit step
   必须有失败用例"的元门禁——新增 step 可以永远不触发。
4. **ADR→gate 矩阵**：`ci/adr-gates.json` 的 `must_gate` 让"决策必须可自动验证"成硬约束。pigweed 无此概念
   （它的等价物是 TODO 里挂 bug 号，弱得多）。
5. **带退出条件的豁免登记**：`ci/exemptions.json` 每条含 `owner/reason/exit/issue`。pigweed 只排除路径，
   没有"这个豁免什么时候必须消失"的字段。
6. **负例 configure 必须失败**：`product-matrix` 里 7 个非法组合（family/board/app/infra/combo/rebind/duplicate）
   断言 `cmake` 配置失败并匹配错误信息。这是对"约束真的在编译期生效"的证明。

## 6. 成本过高、对我们不值得

- **GN+Bazel 双构建体系**：`gn_all`/`gn_*_build_check`/`bazel_build`/`buildifier`/`source_in_gn_build`/
  `bazel lockfile`（`pigweed_presubmit.py:1174` 起）。我们是 C11 + CMake 单构建，抄回来等于养第二套构建。
- **受影响目标的增量构建/测试（Bazel aspect）**：`private/bazel.py:188`，深度绑定 Bazel。
- **JS/TS 生态**：prettier / eslint / npm test / npm vscode（`private/upstream_programs.py:49`）。无 JS 代码。
- **`inclusive_language` 全词表**：技术上零成本，但 `sane/dummy/he/his` 在我们的领域词（如 `dummy` 负载、
  `sane` 默认值）里假阳性概率不低。**这是政策决策不是工程决策**，需要 owner 定词表，不建议静默引入。
- **全树 clang-tidy**：pigweed 靠 Bazel aspect 只扫受影响目标；我们只有一份 `compile_commands.json`，
  全树扫会拖长 `static-analysis` job。建议**按层逐步扩**而不是一次全量。
- **`source_in_cmake_build`**：我们 `check_area_registration.py` + `check_cmake_apps.py` 已覆盖同一风险，
  且更懂我们的 area 约定。不抄。
- **msan / tsan**：嵌入式 host 测试收益有限，`asan/ubsan` 已覆盖主要内存/UB 面。

## 7. 排序后的 5 条提案

1. **加 `check_block_submission.py`**（阻塞短语 + 冲突标记 + `--fix` 提示）
   为什么适合：纯正则、零依赖、塞进现有 `architecture` job 一行；防的是最高代价的失误。
   工作量：**小**（半天内）。风险：**低**——唯一误报来自文档里引用该短语本身，用 `block-submission: ignore` 标签自解。

2. **先补 `LICENSE` + 版权头决策，再加 `check_copyright.py`**
   为什么适合：我们已经"缺许可证"这个事实本身就是风险；pigweed 的 13 行头+错字检测可直接借（`upstream_checks.py:408`）。
   工作量：**小**（脚本）+ **决策**（选许可证）。风险：**中**——必须先决定许可证（Apache-2.0？专有？），
   且一次性给 ~200 个文件加头会淹没 git blame。**建议做法：只对新文件强制，存量文件记入 `ci/exemptions.json` 式登记。**

3. **加 `check_pragma_once.py` + TODO 格式门禁（合成一个 `check_header_and_todo.py` 亦可）**
   为什么适合：两者都 ≤40 行，共用"按扩展名过滤 + 正则"骨架，可复用现有守卫的输出约定。
   工作量：**小**。风险：**低**——先跑一次全树 dry-run 确认存量合规；若不合规，先修存量再开门禁。

4. **给 22 个守卫脚本加 ruff（或 mypy+pylint）CI 步骤**
   为什么适合：守卫脚本是我们全部架构约束的唯一执行者，却是仓库里唯一无 lint 的代码；
   `.ruff_cache/` 说明工具已在环境里，缺的只是配置+job。
   工作量：**小**。风险：**低**——首轮会有告警，可先 `--select` 保守集（E9/F）再放宽。

5. **引入 `keep-sorted: begin/end` 区间检查，只覆盖 `ci/*.json` 与 `ci.yml` matrix 的手排列表**
   为什么适合：`ci/adr-gates.json` 的 gate 表、`.clang-tidy` 的 check 列表、CI matrix 都是手排，
   并行改动必然产生无意义 diff；pigweed 用同一注释机制统一了这点（`keep_sorted.py:38`）。
   工作量：**小到中**（只实现"区间有序断言"约 40 行，不搬 500 行的完整工具）。风险：**低**——
   纯文本检查，不触源码。
