# 外部复审提案的核验（`vendor/proposal.md`）

## 这份文档在做什么

`/workspaces/vendor/proposal.md`（4690 行）是对 `cautonwong/modular` 的一轮外部复审，自称审的是远端
`f2f3da10`。本文逐条核验它的**可核验断言**，并按四条判定之一给出结论：**成立** / **不成立** /
**已过期** / **无法判定**。每条判定都附 `路径:行号` 与所用的命令；没有证据的一律记为「无法判定」。

## 核验的基准：`f2f3da10`，不是本分支

这一点先说清楚，否则整份核验会跑偏：

- 提案点的 `f2f3da10` 在本地存在，且就是 `main` 的 HEAD（`f2f3da1 feat(soc): add RP2040, STM32F4 SoCs and
  board profiles (#147)`）。
- 本文档所在的工作分支是 `bldc`（VESC 固件迁移）。**两条线不是同一棵树**：`pinetime`、`zmk`、
  `rp2040`、`blackpill` 这些 product 只存在于 `main` 一侧；在 `bldc` 上 `product/` 只有
  `example`、`meter_*`、`vesc*`、`riscv_meter`、`water_meter_host`。实测：
  `git ls-tree -r --name-only main -- product/ | grep -c 'pinetime\|zmk'` → 3，而 `bldc` → 0。

因此凡涉及 PineTime / ZMK / Studio / RP2040 / BlackPill 的断言，本文一律用
`git show f2f3da10:<path>` 读它们**当时的源码**来核验，而不是拿 `bldc` 上不存在的文件当反证。

## 判定表

| # | 提案的主张 | 涉及位置 | 判定 | 关键证据 |
| --- | --- | --- | --- | --- |
| 一 | PineTime 主程序构造 28 个 module，只有少数几个调了 `init()`；`edge_sys_start()` 不会代调 | `product/pinetime/main.c`、`sys/runtime/src/sys.c` | **成立** | 见下 §一 |
| 二–十二 | （待核验） | — | 待定 | — |

---

## 一、PineTime 初始化遗漏 —— **成立**（最高优先级）

提案的四条子断言，逐条核验，全部成立：

1. **构造了 28 个 module。** `product/pinetime/main.c:292` 是
   `sys_watch_init(&sys, apps, 28u, required_ids, …)`，而 `apps[0]` 到 `apps[27]` 的 28 个条目
   （`main.c:225-268`）与提案列出的名单**逐字一致**：`touch_gesture`、`button_handler`、
   `watch_power`、`heart_rate`、`step_counter`、`watch_time`、`ble_services`、`alarm`、`stopwatch`、
   `timer`、`watch_settings`、`ble_weather`、`ble_music`、`ble_nav`、`ble_notifications`、
   `ble_motion`、`ble_fs`、`ble_dfu`、`firmware_validator`、`metronome`、`calculator`、`dice`、
   `ble_passkey`、`flashlight`、`game_paddle`、`game_twos`、`paint`、`watch_ui`。

2. **只有 7 个 app 被显式 `init`。** `main.c` 里带失败检查的 `_init()` 调用只有
   `touch_gesture_init`(:97)、`button_handler_init`(:103)、`watch_power_init`(:110)、
   `heart_rate_init`(:116)、`step_counter_init`(:123)、`watch_time_init`(:130)、
   `ble_services_init`(:137)，加上 `pinetime_glue_init`(:13)、`edge_event_queue_init`(:273)、
   `board_pinetime_init`(:282)。**其余 21 个 module 没有任何 init 调用。**

3. **运行时确实不代调。** `sys/runtime/src/sys.c:236` 的 `edge_sys_start()` 里只有调度初始化，
   并且注释自己写着：`:243 /* D51: module init is the composition root's job, so start only
   schedules. */`。它只做 `next_due` / `running` 的赋值然后置 `EDGE_SYS_RUNNING`。契约侧的
   `init`/`deinit` 早已不在 `edge_module_t` 里（`docs/adr-amendments.md:24`、`docs/adr-conformance.md:19`
   的 D15/D51 行），所以没有「运行时兜底」这回事。

4. **CI 抓不到。** `.github/workflows/ci.yml:209`：
   `for product in example meter_host meter_mps2 meter_gateway_host; do` ——
   **pinetime 不在被跑的产品名单里**，所以它可以长期保持这个状态而主 CI 全绿。这条与提案的
   「更严重的是 CI 没抓住」一致。

**不是理论风险的两个实证**（各抽一个 module 读它 init 的真实内容）：

- `app/alarm/src/alarm.c:75` 的 `alarm_init()` 在 `:80-83` 会
  `self->storage->load(self->storage->self, &loaded)` 并从持久化里恢复告警设置。不调 init ⇒ 告警
  永远停在默认值，用户存的设置被静默忽略。
- `app/watch_settings/src/watch_settings.c:80-84` 的 init 同样 `store->load(...)` 恢复设置。这个
  module 正是 `apps[10]`。

**额外发现（提案没点出的更硬的一点）**：`watch_ui` 在 `main.c:236-237` 被 `watch_ui_construct`，
`:268` 挂进 `apps[27]`，`:299-304` 被订阅了四个事件，而且 `EDGE_MOD_WATCH_UI` **在 `required_ids`
（`main.c:285-287`）里**；但 `watch_ui_init(self, display, status_port, event_sink)` 从头到尾没有
被调用。`required_ids` 只做「组装完整性」校验，不代调 init —— 也就是**一个被声明为必需、被订阅了
事件的 module，其初始化端口从未被接上**。这比「少调了几个 init」更容易造成现场故障。

**判定：成立**，且严重度与提案给的「最高优先级」相称。修法也认同提案的方向：把
`<module>_init()` 从「靠人记得写」变成结构（`INIT_OR_FAIL` 宏或一个由 product 提供的 init 列表），
再让 composition-root 检查器与 CI 里真正跑 pinetime 的 job 一起兜住。

---

## 尚未核验（后续按节补）

提案的其余段落：二、ZMK Hold-Tap `quick_tap` 首击误判；三、Macro WAIT 未实现；四、ZMK Studio
Unlock 缺口；五、Studio RPC 吞错；六、RP2040/STM32F4 SoC 驱动是「寄存器模型」；七、RP2040
watchdog feed 语义；八、STM32F4 SPI/I2C；九、BlackPill board 的 system_reset/low-power；十、CI
「只 build 不 run」；十一、静态分析盲区；十二、CodeQL 覆盖；十三、值得保留的 CI 实践；十四、
「门禁强 ≠ 产品正确」；以及第二部分（module ID / events / 低功耗 / wake source 的架构建议 ——
**属设计与主张，不是可核验断言**，本文只核验其中能被证伪的具体事实）与第三部分（3060 行起）。
