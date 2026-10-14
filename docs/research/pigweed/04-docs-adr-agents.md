# 轴四：pigweed 的 SEED 提案流程与面向 agent 的文档 —— 对我们（modular）的借鉴

> 说明：这一轴的子代理报告因异步 `resume` 丢失输出绑定，只有摘要被捕获，全文未落盘。
> 本文件由主代理按**已抽检过的事实**重建（抽检记录见 `00-synthesis.md` 末尾），不是子代理原文。

## 1. pigweed 那边的事实（均已抽检）

| 事实 | 证据（抽检 PASS） |
|---|---|
| 有正式的提案目录 `seed/`，本 checkout 内 **25 篇** `.rst`（编号到 0134，有跳号） | `seed/0000.rst` 存在；`ls seed/*.rst \| wc -l` = 25 |
| 提案元数据是一张被 JSON schema 校验的表，**35 条** | `seed/seed_metadata.json`（35 条）+ `seed_metadata_schema.json` |
| 提案模板有固定七段，其中包含 **Alternatives** | `seed/0002.rst` 的标题序列：Summary / Motivation / Proposal / Problem investigation / Detailed design / Alternatives / Open questions |
| 状态机是显式枚举，含 `Last Call`（固定 7 天）、`Rejected`（也强制归档）、`Accepted`（禁改，同话题另开新提案） | `seed/*.rst` + `seed_metadata.json` 中出现这些状态词 |
| 每个 agent skill 自带 **`TEST.md`** 验收脚本 | `.agents/skills/*/TEST.md` 存在 |

## 2. 我们这边的事实

| 事实 | 证据 |
|---|---|
| `docs/adr.md` 是唯一决策源，但形态是**一张决策行表**（D1–D76） | `docs/adr.md:1404` 附近；状态词只有 `✅`(13) 与 `已冻结`(3) |
| 没有「提案期」的书写位置：没有 `Alternatives`（含不采纳后果）、没有 `rejected` / `superseded-by` 终态 | 同上（全文没有这些状态词） |
| 面向 agent 的文档共 9 篇（`docs/agents/` 2 篇 + `docs/how-to/` 7 篇），**都没有可验收小节** | `ls docs/agents docs/how-to` |
| 我们的 gate 矩阵（`ci/adr-gates.json` + `must_gate`）比 pigweed 硬 | `ci/adr-gates.json`、`docs/governance.md` |

## 3. 结论

我们缺的不是"决策记录"，是**决策的提案期与终态**。pigweed 的 SEED 把三件事强制下来，三件都能低成本移植：

1. **提案必须写替代方案与"不采纳会怎样"** —— 我们只有冻结后的结论行，评审时看不到被否掉的路；
2. **终态必须显式**（`rejected` / `superseded-by N`）—— 我们的表里没有"被取代"的表达，只能靠人记；
3. **提案期是 issue 而不是文档** —— 与我们的 issue tracker（`cautonwong/modular`，见 `docs/agents/issue-tracker.md`）天然契合。

判断依据：`docs/bldc-migration.md` 的 §2/§4 已经在手工实践"判据 + 工作方法 + 偏差去向"这套形状，
说明我们**已经需要它**，只是没有固定格式，也没有人来校验格式。

## 4. 提案（排序后）

| # | 做什么 | 为什么适合我们 | 工作量 | 风险 |
|---|---|---|---|---|
| 1 | 决策行增加两个槽：`Alternatives`（含"不采纳的后果"）与终态（`rejected` / `superseded-by N`） | 让评审看到被否掉的路；`adr-amendments.md` 已有"修订"概念，只差终态词 | 小（1–2 小时，改 `docs/adr.md` 模板 + 已有行补 1 条示例） | 低。补历史行会很啰嗦 → **只对新决策强制**，历史行不动 |
| 2 | 提案期落到 issue 模板：三类触发（新增层/端口契约变更/冻结原则相关）必须先开 issue，且 issue 必须含 `Alternatives` 段 | 我们已有 issue tracker 与 triage 标签体系，只差模板字段 | 中（模板 + 一条 CI 检查：决策行必须引用 issue 号） | 中。CI 检查会先爆出现有决策行没号 → 先 `--report` 统计再加门禁 |
| 3 | 面向 agent 的文档加"可验收小节"（能跑的命令 + 期望输出） | `docs/how-to/` 7 篇里 `add-app.md`/`add-driver.md` 都是过程，读者（人或 agent）无法自证做对了 | 小（每篇 3–5 行） | 低。只给**含命令的**文档加，纯说明性文档不加 |
| 4 | 把 `docs/bldc-migration.md` 的 §2（判据）与 §4（工作方法）抽成通用模板 | 这套形状已经在真实迁移里跑通，其它阶段可复用 | 小 | 低 |
