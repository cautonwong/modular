# pigweed → modular：可执行借鉴清单（合成）

> 来源：5 条只读研究 lane（facades / module-structure / presubmit / docs-adr / testing）的独立报告，
> 同目录 `01`–`05`。本文件是**主代理的合成与抽检结论**，不是任何单个子代理的观点。
> 所有 pigweed 侧引用都经过路径级抽检（记录见 §6）。

## 0. 一句话结论

pigweed 值得借的是**记账与门禁的纪律**，不值得借的是**它的构建期机制**：它是 C++/Bazel 生态的
编译期依赖注入，我们是 C11 + 单一 CMake 的构造期注入。三条最值钱的借鉴，恰好都打在我们自己承认的痛点上：

1. 依赖边要**分档**（私有边 vs 可传导边）—— 我们的 `DEPS` 全 PUBLIC，正在破坏 D27「精确 include 隔离」；
2. **守门的脚本本身没人守** —— 24 个 `check_*.py` 无 lint、无类型检查、无 CI；
3. **决策只有结论、没有提案期** —— `docs/adr.md` 是决策行表，缺 `Alternatives` 与终态（rejected / superseded-by）。

## 1. 建议直接做（低成本，打真实痛点）

| # | 做什么 | 证据 / 为什么 | 工作量 | 风险 |
|---|---|---|---|---|
| A1 | 24 个守卫脚本加 ruff（+ 关键几个加 mypy）与一个 CI step | 仓库里已有 `.ruff_cache/` 但**无 ruff 配置、无 CI step**；pigweed 对 Python 跑 mypy+pylint | 小（半天） | 低。会先冒告警 → 先 `--report` 看数量 |
| A2 | 依赖边分档：`DEPS` 改 PRIVATE，新增 `PUBLIC_DEPS`；只有"公开头 include 了它"才写后者 | 对齐 Bazel `implementation_deps` / GN `public_deps`（pigweed 比我们多的**第一个真东西**）。改完漏登记立刻变**编译错误**，且不需要任何新元数据 | 小（半天） | 中。存量会真爆漏登记的边 → 先加 `--report` 统计爆炸半径，按 area 分批切 |
| A3 | 决策行加 `Alternatives`（含"不采纳的后果"）+ 终态 `rejected` / `superseded-by N` | pigweed SEED 模板固定七段含 Alternatives；我们 `docs/adr.md` 全文只有 `✅`(13) / `已冻结`(3)，无"被取代"的表达 | 小（1–2 小时） | 低。**只对新决策强制**，历史行不动，否则补写量巨大 |
| A4 | TODO 必须带 issue 号；`keep-sorted: begin/end` 区间断言有序 | 我们有 `ci/adr-gates.json` 的 gate 列表、`.clang-tidy` 的 check 列表、`ci.yml` matrix，都是手排列表，会漂 | 小 | 低 |
| A5 | 面向 agent 的文档（`docs/agents/` 2 篇 + `docs/how-to/` 7 篇）加"可验收小节"：能跑的命令 + 期望输出 | pigweed 每个 skill 自带 `TEST.md` 验收脚本（已抽检 PASS）；我们的 9 篇都无自证方式 | 小（每篇 3–5 行） | 低。只给**含命令**的文档加 |

## 2. 值得做，但有前置条件

| # | 做什么 | 前置条件 | 工作量 | 风险 |
|---|---|---|---|---|
| B1 | 把测试里重复的 `static` mock 提升为模块内独立构建目标 `*_fake.c`（只记录）与 `*_mock.c`（会判对错） | 先定死命名与语义（二者混用会灾难）；新库要挂 `edge_enable_quality`，否则覆盖率会掉 | 小 | 低。pigweed 的 `pw_digital_io.digital_io_mock` 是独立 `pw_source_set`/`pw_add_library`，任何模块可复用 |
| B2 | 模块级可见性白名单 `ci/module-visibility.json`（受限模块 → 允许依赖它的模块/层；双向校验，含"过期条目"） | 首版白名单必须如实反映现状，否则一上线就红 | 中（1–2 天） | 低-中。**与 D7（拒绝产品 manifest）不冲突**：D7 管产品组成，这条管模块可见性 |
| B3 | 每模块一行元数据 `ci/modules.json` + JSON schema | **必须有 ≥2 个真实消费者**（覆盖率豁免 / CI 矩阵 / 文档索引），否则几个月后变成第二份过期清单 | 中（1 天） | 中。`status` 取值要先与 ADR 对齐 |
| B4 | 给 app 单测引入可注入时钟（现在只能按步数近似 period/timeout） | 只先在 1–2 个 app 上验证，别变成新的隐藏全局状态 | 中 | 中。pigweed 的 `SimulatedSystemClock` 证明 host 端可行 |
| B5 | `_Static_assert` 的**负面编译测试**（宏化 + CMake "预期编译失败" helper） | 需与 `WILL_FAIL`（运行期失败）区分开，别混用 | 中 | 中。C11 可覆盖的失败点比 C++ 少 |

## 3. 明确不借（附代价）

1. **整套 facade/backend 机制**：它是*编译期择一实现*，我们是*构造期注入*；pigweed 自己在
   `docs/sphinx/facades.rst:145-149` 就说"能负担依赖注入就不要用 facade"。照搬等于把"谁被装配"
   从组合根 `main()` 搬到构建系统里 —— 直接违反我们冻结的**组合根**原则。
2. **`*_public_overrides/` 头覆盖**：依赖 include 路径顺序、同名头互相覆盖；pigweed 自嘲为
   "things hiding under rocks"。会破坏我们 `check_app_transitive_includes.py` 的可发现性。
3. **三套构建并存（GN/Bazel/CMake）**：他们**自己已有漂移**（`pw_base64` 只在 Bazel 声明了
   `implementation_deps`，GN/CMake 靠传递依赖侥幸编过）。我们只有一套 CMake，**这是优势**。
4. **host/target `.lib/.bin` 双测拆分**：收益取决于"我们真有多少测试能在 target 上跑"；在拿到
   1–2 个已跑通的 target 测试之前不建议动。
5. **许可证头 / 供应链那一套**：需要先有**法务与许可决策**，不是工程问题。

## 4. 我们比它强的地方（pigweed 该学我们）

| # | 我们有什么 | pigweed 的情况 |
|---|---|---|
| 1 | **固件体积三层门禁**：ELF flash/RAM（`check_size.py`）+ 按层 map 预算（`check_map_budget.py` + `ci/size-budget.json`）+ 趋势基线（`report_size_trend.py` + `ci/size-baseline.json`），外加静态栈深与零动态内存 | presubmit **没有任何 size/bloat 门禁**；`pw_bloat` 只是报告库 |
| 2 | **可复现构建 + SBOM**：`SOURCE_DATE_EPOCH` + 两次构建 `cmp` + `generate_sbom.py` | 无对应 presubmit 步骤 |
| 3 | **守卫的元门禁**：`check_guard_coverage.py` 强制每个 `check_*.py` 同时有 `expect_pass=True/False` 夹具 | 有模块单测，但**没有**"每个 step 必须有失败用例"的元门禁 → 新 step 可以永远不触发 |
| 4 | **差分 harness**：直接编译参考固件与端口逐位比对（`docs/bldc-migration.md`） | 其 Bazel 环境反而做不到这一点 |

## 5. 建议落地顺序（按性价比）

1. **A1** 守卫脚本 lint（半天，先把告警数量量出来）
2. **A2** 依赖边分档（半天，先 `--report` 统计爆炸半径，再按 area 分批）
3. **A3** ADR 的 Alternatives + 终态（1–2 小时，只对新决策）
4. **A4/A5** TODO 门禁 + 文档可验收小节（各 1–2 小时）
5. **B1** 替身提升为模块目标（半天，从已有重复 mock 的 `foc`/`flash`/`relay` 开始）
6. **B2** 模块级可见性白名单（1–2 天）
7. **B3** 模块元数据表（1 天，**先确定两个真实消费者**）

## 6. 抽检记录（子代理的话不算证据）

pigweed 侧引用 13 处全部 **PASS**：`docs/sphinx/facades.rst:145-149`（依赖注入优先）、
`module_structure.rst:170-202`（public_overrides 段）、`pw_module/.../check.py:151/169`（PWCK002/004）、
`pw_presubmit/.../cpp_checks.py:47`（pragma 检查）、`pw_digital_io/BUILD.gn:65-70`（`digital_io_mock` 目标）、
`pw_chrono/.../simulated_system_clock.h`（`SimulatedSystemClock`）、`seed/0000.rst` + `seed_metadata.json`(35)
+ `seed_metadata_schema.json` + `seed/0002.rst`(含 Alternatives) + `Last Call/Rejected/Accepted` + `.agents/skills/*/TEST.md`。

抓到 **3 处偏差**（已在本文件中按事实修正）：

| 偏差 | 报告写的 | 实测 |
|---|---|---|
| 守卫脚本数量 | 22 | **24**（pre-commit 挂 16） |
| SEED 篇数 | `0000.rst…0134.rst` | 本 checkout **25 篇** `.rst`（编号有跳号），元数据 **35 条** |
| facade 是否适合我们 | 一度被当作"可借机制" | **不借**（编译期择一 ≠ 构造期注入；pigweed 自己也这么说） |

另：`04-docs-adr-agents.md` 的完整报告因异步 `resume` 丢失输出绑定未被捕获，该文件由主代理按上表已核事实重建。
