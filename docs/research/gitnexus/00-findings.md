# GitNexus 实测评估（对 modular）

> 目的：回答三个问题 —— ①它有 skill 和 MCP 吗？②对「解析 modular」这个目标有多大帮助？③省 token 吗？
> 方法：`pnpm dlx gitnexus@1.6.12` 在 `/tmp` 的 modular 副本上实际索引，并跑真实问题与**已知答案**对照。
> 所有数字都是本次实测，不是它的宣传。

## 1. 它是什么（实测）

| 项 | 实测值 |
|---|---|
| 运行时依赖 | node 22 + pnpm；包解包 230 MB、39 个依赖；`@ladybugdb/core`（图数据库，有 linux-x64 预编译） |
| 索引我们的仓库 | **65.9 秒**（副本 25 MB、301 个 C/H 文件）；安装+解析合计 1m28s（包已缓存后） |
| 产出规模 | **6,385 节点 / 9,431 边 / 110 簇 / 54 流** |
| 产物位置 | 仓库内 `.gitnexus/`（本次在 `/tmp` 副本里，未污染工作区） |
| 许可证 | **PolyForm Noncommercial 1.0.0**（商业化使用需另行授权 —— 这是决策，不是工程） |

## 2. 它有 skill 与 MCP —— 有，而且包装得比内核好

**12 个 agent skill**：`gitnexus`（总入口）、`-cli`、`-exploring`、`-guide`、`-impact-analysis`、
`-debugging`、`-refactoring`、`-review`、`-pr-swarm-review`、`-plan`、`-work`、`-lfg`。

**三种宿主集成**：`gitnexus-claude-plugin/`、`gitnexus-cursor-integration/`、`gitnexus-factory-plugin/`；
`gitnexus setup` 会往 Cursor / Claude Code / Antigravity / OpenCode / CodeBuddy / Qoder / Codex 写 MCP 配置，
`gitnexus uninstall` 可完全撤销。

**MCP 服务**：`gitnexus mcp`（默认 stdio；`--http` 起 **Streamable HTTP `POST /mcp`** + 旧式 SSE）。
**CLI 与 MCP 共用同一套能力**：`query`（按概念搜执行流）、`context`（符号 360 度视图）、
`impact`（改动爆炸半径）、`trace`（两符号间最短调用路径）、`cypher`（直接查图）、
`detect-changes`（git diff → 受影响符号与执行流）、`check`（对图跑结构检查）、
`wiki`（从图生成仓库 wiki）、`group`（跨仓库 impact）。
其中 `query`/`context`/`impact` 支持 `maxTokens`，响应有界且用「4 字节≈1 token」的确定性估算截断
—— 这是它最值得抄的设计。

## 3. 但 C 侧的图是**降级**的（它自己的日志说的）

索引时它主动打了三类警告，我原文摘录其含义：

1. `name-fallback resolution: 2390 call sites (899 distinct caller-file/name pairs), 0 refused`
   → 调用边是**按名字猜**出来的，不是按 include/作用域解析的（与它语言表里 C 的 `Imports ✗` 一致）。
2. `callable-value-flow: candidate set exceeded the cap; no partial CALLS emitted`（33 > cap 32）
   → 候选集超限时它**整批丢弃** CALLS 边，而不是部分保留。多处在 `sys/runtime/src/sys.c`、`tests/test_sys.c`。
3. `[scope-resolution] 5654 property read/write site(s) name a field that IS defined in this workspace,
   but only in another language … Affected: adc_throttle_v (cpp), allow_braking (cpp), …`
   → 它把**我们的 C 结构体字段当成 cpp**处理，字段级读写的边因此不可靠，而且它自己提醒：
   这类查询返回空**不等于**"没被用到"。

## 4. 省 token 吗？—— 本次两个真实问题的对照

| 问题 | GitNexus | 我现有的工具 | 谁更省 |
|---|---|---|---|
| 「motor_config 存配置到变量存储」 | `query` 返回 **5921 字节** JSON：进程列表 + 带 `startLine/endLine` 的符号指针 + 调用链 | `symbol_search` + `module_report`（只读大纲）给同类指针 | **持平**（形状相似，它能排执行流是加分） |
| 「改 `foc_observer_adjust_params` 会炸到谁」 | `impact` 返回 **2264 字节**，但状态是 `ambiguous`、`impactedCount: null`、`risk: UNKNOWN`、`enrichment-skipped`（processes/modules 两轴都被跳过） | `grep` 返回 **1735 字节**，且**结论明确**：调用点集中在 `app/foc_core/src/foc_core.c` 与 `tests/test_foc_core.c` | **现有工具更快下结论** |

**结论：对我们这个 C11 仓库，它不省 token。** 省 token 取决于"答案是否定论"，而 C 侧它经常给定论不了的答案
（名称消歧、丢边、字段跨语言误判）。它声称的 "no 10-query chains" 在它语言表里 `Imports` 与 `Heritage`
齐全的语言（TS/JS/Python/Java/Kotlin/C#/Go/Rust）成立，在 C/C++ 上不成立。

## 5. 处置建议

- **不引入**（对当前 goal 无净收益，且许可证是商业化的硬门槛）。
- **可抄的两点**：①工具响应带 `maxTokens` 上界与确定性估算（我当前的 tool 输出已经是有界的，保持住）；
  ②从图生成 wiki 的思路 —— 我们已有「docs 即事实源 + 生成物由 `--check` 守一致性」的更严版本。
- 若要再评一次：先解决许可证，再把索引规模限制在实际关心的子树上（不要全仓），并优先用它支持良好的语言
  （我们的 Python 工具链、或任何未来的 TS 前端），而不是 C。

## 6. 复现命令（本次实验）

```bash
cp -a /workspaces/vendor/modular /tmp/gnx-test/modular   # 不要在真仓库里建索引
cd /tmp/gnx-test/modular
GITNEXUS_SKIP_OPTIONAL_GRAMMARS=1 pnpm --allow-build=@ladybugdb/core \
  --allow-build=gitnexus --allow-build=tree-sitter dlx gitnexus@latest analyze .
gitnexus query "motor_config save config to variable store"    # 5921 字节
gitnexus impact foc_observer_adjust_params                     # 2264 字节，结论 ambiguous
grep -rn foc_observer_adjust_params --include=*.c --include=*.h .   # 1735 字节，结论明确
```

注意：**副本必须有 `.git`**，否则 `analyze` 直接以「Not a git repository」退出（退出码 1）；
或者显式加 `--skip-git`。
