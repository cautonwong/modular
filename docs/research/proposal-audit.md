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
| 二 | ZMK Hold-Tap 的 `quick_tap` 把开机后首击误判为连击（`last_tap_time_ms` 初值 0） | `app/zmk_behavior/src/behavior.c` | **成立** | 见下 §二 |
| 三 | Macro 的 `WAIT`（`wait_ms` / `default_wait_ms`）声明了但没实现 | `app/zmk_behavior` | **成立** | 见下 §三 |
| 四 | ZMK Studio 的 `UNLOCK_DEVICE` 无任何认证/物理前提，直接 `unlocked = true` | `app/zmk_studio/src/studio.c` | **成立** | 见下 §四 |
| 五 | Studio RPC 吞掉 `keymap` 返回的错误，客户端收到 SUCCESS | `app/zmk_studio/src/studio.c` | **成立（一处措辞需更正）** | 见下 §五 |
| 六 | RP2040/STM32F4 的 SoC 驱动是寄存器/RAM 模型，SPI/I2C 不是硬件通信 | `soc/rp2040`、`soc/stm32f4` | **成立** | 见下 §六 |
| 七 | RP2040 watchdog 的 feed 没有重装计时器（只置 ENABLE） | `soc/rp2040/src/soc_rp2040.c` | **成立** | 见下 §七 |
| 八 | STM32F4 的 SPI/I2C 停在模型层，I2C 地址被丢弃 | `soc/stm32f4/src/soc_stm32f4.c` | **成立** | 见下 §八 |
| 九 | board 的 `system_reset` / `low_power` 只是计数器（host model） | `board/{blackpill,colmi_p8,nice_nano,rpi_pico}` | **成立** | 见下 §九 |
| 十 | CI 大量产品「只 build 不 run」 | `.github/workflows/ci.yml` | **成立** | 见下 §十 |
| 十一 | clang-tidy 是手工白名单，新增代码不会自动进入审查 | `.github/workflows/ci.yml:396-416` | **成立** | 见下 §十一 |
| 十二 | CodeQL 的构建矩阵不覆盖 pinetime/ZMK/新板运行时 | `.github/workflows/codeql.yml:36-40` | **不成立（措辞过宽）** | 见下 §十二 |
| 十三 | Part 3：当前中断架构 = ISR → event queue（D3/D17） | `docs/adr.md:37/:51/:798/:1156` | **成立** | 见下 §十三 |
| 十三（b）| Part 3：UART RX 每字一 event 造成事件风暴 | `infra/uart/uart.h:20-21` | **不成立（现状无此路径）** | 见下 §十三 |
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

## 二、ZMK Hold-Tap 的 `quick_tap` 首击误判 —— **成立**

代码与提案引用的完全一致：`app/zmk_behavior/src/behavior.c:220-221`

```c
if (ht->config.quick_tap_ms > 0 &&
    (timestamp_ms - ht->last_tap_time_ms) < ht->config.quick_tap_ms) {
```

而 `last_tap_time_ms` 的唯一写入点是 **释放路径里的 tap 分支**（`:259`）：

```c
} else {
    /* Tapped */
    ht->last_tap_time_ms = timestamp_ms;
```

构造时整个结构体被清零（`:30 *self = (__typeof__(*self)){0};`），所以开机后它的值是 0；`timestamp_ms`
是调用方传进来的毫秒计数（`:207 self->current_time_ms = timestamp_ms;`，`poll` 再用它 tick），开机后
很小。两者相减必然小于 `quick_tap_ms`，于是**开机后 `quick_tap_ms` 之内的第一次按下**就走进了
「Double tap」分支（`:222-226`）并立刻 `return EDGE_OK`。

**我把后果说得比提案更准一点。** 提案说「直接执行 tap behavior」，但按纯 tap 走的话普通点按最终
也会触发 tap，所以真正的损坏在它的**副作用**上：该分支设了 `is_tapped = true` 就返回，没有设
`ht->active`、`ht->is_held`、`ht->press_time_ms`。于是

1. 这一次如果用户其实是**按住**（想触发 hold），释放时 `ht->is_tapped` 已被清掉、`ht->active` 又是
   false，两个分支都不进，`handle_key_press` 在 `:262-270` 一路 `return EDGE_OK` —— **这一按的 hold
   被静默吞掉**。
2. 走 `tapping_term` 的那条路也不可能发生，因为 `ht->active` 从未置位、`press_time_ms` 从未写入。

窗口只限开机后 `quick_tap_ms` 毫秒内（`timestamp_ms` 会增长，之后相减自然超过阈值），所以危害是
「开机初期头一次按键、且是长按」这种场景 —— 恰好是上电后最容易发生的那一次。

**没有测试覆盖、也没有测试把这当成预期。** `tests/test_app_zmk_behavior.c` 里 quick-tap 的用例
（`:308-330`）第一次按用的是 `timestamp = 500`、`quick_tap_ms = 150`，`500 - 0 = 500` 不触发 ——
它只是**恰好**没踩到开机窗口。把那个 500 换成 100，这条用例会立刻失败。

**判定：成立**。修法方向认同提案：加一个「有没有前一次 tap」的标志（或有效的 sentinel 时间戳），
而不是拿 0 当合法时间戳。

---

## 三、Macro 的 `WAIT` 没有实现 —— **成立**

声明的部分都在，提案没有编：

- `app/zmk_behavior/include/zmk_behavior/behavior.h:110`：`ZMK_MACRO_ACTION_WAIT = 3,`
- 同文件 `:117`：`uint16_t wait_ms;`（在 `zmk_macro_step_t` 里）
- 同文件 `:123`：`uint16_t default_wait_ms;`（在 `zmk_macro_config_t` 里）

执行的部分没有：`app/zmk_behavior/src/behavior.c:347-364` 的 `case ZMK_BHV_MACRO` 是一个同步循环：

```c
for (uint8_t s = 0; s < macro->step_count; s++) {
    const zmk_macro_step_t *st = &macro->steps[s];
    if (st->action == ZMK_MACRO_ACTION_PRESS) { … }
    else if (st->action == ZMK_MACRO_ACTION_RELEASE) { … }
    else if (st->action == ZMK_MACRO_ACTION_TAP) { … }
}
```

**没有 WAIT 分支，也没有任何一处读 `st->wait_ms` 或 `macro->default_wait_ms`。** 全仓搜索
`git grep -n "wait_ms\|ZMK_MACRO_ACTION_WAIT\|default_wait" f2f3da10 -- app/ tests/` 的结果里，
宏相关的只有上面三个声明；剩下命中全在 `app/zmk_behavior_queue`：

- `app/zmk_behavior_queue/src/behavior_queue.c:117-119`：
  `if (item.wait_ms > 0) { … app->next_run_time_ms = current_time_ms + item.wait_ms; }`

也就是说：**等待能力在同一个仓里已经有一份可用实现，而在宏这里只是一个字段**。提案的
「这个需要和 `zmk_behavior_queue` 统一」不是风格建议，是「两套机制、一套真的、一套是壳」。

补充两点：

1. 因为整个循环用同一个 `timestamp_ms` 同步跑完，说它是「同步 key sequence executor」而不是
   「timed macro engine」是准确的 —— 所有步骤在同一毫秒里发完，包括 TAP 的按下与释放。
2. 没有任何测试碰过 `wait_ms`（`tests/` 下与 `wait_ms` 相关的断言全部属于 behavior_queue），
   所以也没有测试把这个行为当成预期。

**判定：成立**。

---

## 四、ZMK Studio 的 Unlock —— **成立**（安全边界确实不存在）

提案引的那两行一字不差，而且周围确实什么都没有：

`app/zmk_studio/src/studio.c:64-68`

```c
case ZMK_STUDIO_CORE_CMD_UNLOCK_DEVICE: {
    app->unlocked = true;
    resp[2] = 0;
    resp[3] = 1;
    resp_len = 4;
    break;
}
```

逐项核验提案列的「没有」清单，**四项全部成立**：

| 提案说没有 | 实测 |
| --- | --- |
| physical unlock | `app/` 下没有任何 `studio_unlock` behavior/按键组合；`unlocked` 的全部读写都在 `studio.c` 自己里（见 `git grep -n unlocked f2f3da10 -- app/ tests/` 的结果） |
| authentication / challenge / token / pairing | 没有。处理函数不看任何输入，不看传输来源，不看 `rx_state`，只写一个 bool |
| timeout（空闲回锁） | `grep -n "timeout\|timer\|idle" app/zmk_studio/src/studio.c` **无任何命中**；struct 里也没有计时字段 |
| disconnect → lock | 三处把 `unlocked` 置 false 的地方分别是：`app_power_off()`（`:218`）、construct（`:256`）、`zmk_studio_init()`（`:266`）。**没有一处是断连或空闲触发的** |

而这次不是「什么都没做」——**机制都在，只有钥匙是白送的**：`studio.h:40-41` 定义了
`UNLOCK_DEVICE = 3` 与 `LOCK_DEVICE = 4`，`studio.c:79 / :136 / :172 / :186` 四个写命令都真的检查了
`if (!app->unlocked)` 并返回 Locked 错误，`GET_LOCK_STATE`（`:59-62`）也能报状态。也就是说，
**锁的作用域是真的，但锁的开启无任何前提** —— 任何能发 RPC 的一端（USB / BLE）发一个
`UNLOCK_DEVICE` 就拿到写权限。

**测试反而把这个行为固定下来了**：`tests/test_app_zmk_studio.c:141`
`assert_true(zmk_studio_is_unlocked(&app));` —— 它在上一条 RPC 发出后直接断言已解锁，
没有任何一条用例检查「未物理解锁时 unlock 应被拒」。

**判定：成立**。修法方向也认同提案：把安全状态的变更从 RPC handler 里拿出来，换成
`studio_unlock_authorize()` / `studio_lock()` 这类入口，再由物理侧（behavior）与超时/断连
去驱动它。

---

## 五、Studio RPC 吞错 —— **成立**（但提案的「完全没有检查」过宽，一处需更正）

逐条对过 `app/zmk_studio/src/studio.c`，五处 keymap 调用里**四处吞、一处检查**：

| 位置 | 调用 | 返回值的处理 |
| --- | --- | --- |
| `:121-125` | `get_layer_binding(...)` | **吞**。返回值直接丢弃，然后 `resp[2] = 0;` |
| `:152-160` | `set_layer_binding(...)` | **有检查**。`edge_status_t rc = …; resp[2] = (rc == EDGE_OK) ? 0 : 2;` |
| `:177-181` | `save_changes(...)` | **吞**（提案引的就是这一处） |
| `:191-195` | `discard_changes(...)` | **吞** |
| `:84-88` | `discard_changes(...)`（RESET_SETTINGS） | **吞** |

所以「返回值**完全没有**检查」不成立（`set_layer_binding` 就是一个反例，它甚至把失败映射成了错误码 2），
但**提案点名的两处、加上刷新设置那一处，逐字成立**：

```c
if (app->keymap.save_changes != NULL) {
    app->keymap.save_changes(app->keymap.self);   /* 返回值丢弃 */
}
app->unsaved_changes = false;
resp[2] = 0;                                      /* 永远 SUCCESS */
```

**比提案说的还差一档**：底层返回 `EDGE_EIO` 时，`unsaved_changes` 还是被清成 `false`。
也就是说不仅客户端以为存了，**设备自己也认为没有未保存改动**，后续不会再有任何重试或告警路径
—— `GET_UNSAVED`（`:166-167`）也会回报 0。这正是提案描述的那种最难查的不一致，评级 P1/P2 合理。

**测试看不见它**：`tests/test_app_zmk_studio.c:60-70` 的 mock `save_changes` / `discard_changes`
**永远 `return EDGE_OK`**，全文件对失败路径的注入次数为 0
（`grep -c "EDGE_EIO\|fail"` → 0），所以这个缺陷在 CI 里永远不会现身。

**判定：成立**（并附上「`set_layer_binding` 已正确检查」这一处更正）。

---

## 六、SoC 驱动是「寄存器模型」—— **成立**（两个芯片都成立）

### RP2040：连基址都没有

`soc/rp2040/` 只有两个源文件（`include/soc_rp2040/soc_rp2040.h`、`src/soc_rp2040.c`）。对它搜
MMIO 的标志物：

```text
git grep -n "0xd0000000\|0xD0000000\|SIO_BASE\|volatile" f2f3da10 -- soc/rp2040/
```

→ **无任何命中**。也就是说那套 `..._sio_regs_t` / `..._spi_regs_t` 结构体从来到尾都是普通的
RAM 结构体，没有一处被指到 0xd0000000 上。调用方式是 `board_*_hw_t hw; board_*_hw_init(&hw);`
（例：`board/pinetime/include/pinetime/board.h:58`、`board/nice_nano/...:30`），即**寄存器是调用方
传进来的内存**。

提案引的两段一字不差：

- SPI：`soc/rp2040/src/soc_rp2040.c:93-95` —— `spi->sspdr = tx_byte;` 紧接着
  `rx_buf[i] = (uint8_t)(spi->sspdr & 0xFFu);`（写进去、马上从同一个字段读回来）
- I2C：同文件 `:122` 与 `:137-138` —— `i2c->ic_data_cmd = cmd;` 后
  `data[i] = (uint8_t)(i2c->ic_data_cmd & 0xFFu);`

真 SPI 需要 TX/RX FIFO、busy、FIFO 空满、时钟与 CS；真 I2C 需要 START/ADDR/ACK/BUSY/STOP。
这里两者都是「写一个字段、立刻读回同一个字段」，既没有 FIFO 也没有状态位。

### STM32F4：有基址，但基址从未被用

这一半比 RP2040 更值得单说，因为它**看起来**是硬件驱动：

- `soc/stm32f4/include/soc_stm32f4/soc_stm32f4.h:21-24` 定义了 `SOC_STM32F4_SPI2_BASE
  ((uintptr_t)0x40003800u)`、`I2C1_BASE`、`I2C2_BASE`、`IWDG_BASE`，结构体成员也带了
  `volatile uint32_t moder; /* 0x00 */` 这种真实偏移注释。

但对全仓搜 `SOC_STM32F4_SPI2_BASE` / `SOC_STM32F4_I2C1_BASE`，**只命中那两行宏定义本身**，
没有任何解引用。函数签名全部收调用方给的指针：
`soc_stm32f4_spi_init(soc_stm32f4_spi_regs_t *spi, …)`（`:125`）、
`soc_stm32f4_spi_transfer(soc_stm32f4_spi_regs_t *spi, …)`（`:126`）、
`soc_stm32f4_i2c_write(soc_stm32f4_i2c_regs_t *i2c, …)`（`:131`）。

于是 SPI 的传输体（`soc/stm32f4/src/soc_stm32f4.c:71-76`）与 RP2040 完全同形：

```c
uint8_t tx_byte = tx_buf ? tx_buf[i] : 0xFFu;
spi->dr = tx_byte;
if (rx_buf != NULL) {
    rx_buf[i] = (uint8_t)(spi->dr & 0xFFu);
}
```

### 两点补充（提案没提，但影响判定的分量）

1. **仓库自己也从未把它标注为模型。** 搜 `docs/` 下的 `register model` / `mmio` / `host model` /
   `simulat`，没有任何一处声明「这是主机模拟、不是硬件驱动」；`-- soc/` 下也没有 README。
   名字叫 `soc_*`、注释写着真实偏移，读者会自然以为是可用的 HAL —— 这就是问题所在。
2. **它们几乎不报错。** RP2040 的三个入口在结尾都是无条件 `return true;`
   （`soc_rp2040.c:98`、`:124`、`:140`），只有入参检查会 `false`。

**判定：成立**，且对两个芯片都成立（提案第八节对 STM32F4 的说法是同一件事的另一种表述，两项应合看）。
方向上可接受的做法只有两种：要么把它做成真的 MMIO 驱动（`volatile` + 真实基址 + FIFO/状态位等待），
要么在头文件里把「这是主机模型（host model）、不驱动硬件」写清楚 —— 而不是让一个叫 `soc_stm32f4_spi_transfer`
的函数在真板上静默跑成一个内存拷贝。
---

## 七、RP2040 watchdog 的 feed —— **成立**

`soc/rp2040/src/soc_rp2040.c:59-64`：

```c
void soc_rp2040_wdt_feed(soc_rp2040_wdt_regs_t *wdt) {
    if (wdt == NULL) {
        return;
    }
    wdt->ctrl |= 0x40000000u;
}
```

与提案引用的一字不差。RP2040 的 `WATCHDOG_LOAD` 才是重装计数的寄存器，而这里**只或了一下 CTRL 的
ENABLE 位**，从不写 `load`。

两个旁证说明这不是「没写完」而是「写错了」：

1. **作者知道 `load` 存在且会用**：同文件 `:55` 的 `wdt_start` 里就是 `wdt->load = delay_ms * 2000u;`，
   结构体也在 `soc_rp2040.h:31` 定义了 `load`。
2. **它真的被调用**：`board/rpi_pico/src/board.c:51` 走的就是 `soc_rp2040_wdt_feed(&g_hw_inst->wdt);`，
   属于周期性喂狗路径。

**测试反而把错误语义固定下来了**：`tests/test_soc_rp2040.c:42-43`

```c
soc_rp2040_wdt_feed(&wdt);
assert_int_equal(wdt.ctrl & 0x40000000u, 0x40000000u);
```

它只断言 ENABLE 位还在，**没有任何一句检查 `load` 被重装**。换句话说：现有 CI 对这个函数「正确」的
定义，正好就是那个不喂狗的行为。

**另外一处观察（需外部数据手册核实、本文不作为判定依据）**：`:55` 把 `delay_ms` 乘以 **2000**
装入 `load`（1000 ms → 2,000,000）。RP2040 的 watchdog 计数常以微秒为单位，按此应是 1,000,000；
若属实则超时也是 2 倍。这一点我没有在本地资料里核实，只列为线索。

**判定：成立**。

## 八、STM32F4 的 SPI/I2C —— **成立**

SPI 部分与 §六 同一个事实（`soc/stm32f4/src/soc_stm32f4.c:73-75` 的写—回读），不重复。
I2C 部分提案的说法更硬，逐行核到：

`soc/stm32f4/src/soc_stm32f4.c:90-100`

```c
bool soc_stm32f4_i2c_write(soc_stm32f4_i2c_regs_t *i2c, uint8_t addr, const uint8_t *data,
                           size_t len) {
    if (i2c == NULL || (data == NULL && len > 0u)) {
        return false;
    }
    (void)addr;                              /* 从机地址被显式丢弃 */
    for (size_t i = 0u; i < len; ++i) {
        i2c->dr = data[i];
    }
    return true;
}
```

`(void)addr;` 不是推断，是字面上存在的：全仓搜 `(void)addr` 只命中两处，`:95`（write）与 `:106`（read）。
**一份 I2C API 接收从机地址，然后把地址丢掉** —— 多从机总线上指不定写到哪个设备，而函数无条件
`return true`。读函数同形。

**判定：成立**。

---

## 九、board 的 `system_reset` / `low_power` —— **成立**（而且是有意的）

提案举的例子字字属实，四个板都一样：

| 板 | 函数 | 位置 | 函数体 |
| --- | --- | --- | --- |
| blackpill | `board_blackpill_system_reset` | `board/blackpill/src/board.c:69-71` | `++g_reset_count;` |
| blackpill | `board_blackpill_enter_low_power` | 同文件 `:58-60` | `++g_low_power_entries;` |
| colmi_p8 | `board_colmi_p8_system_reset` | `board/colmi_p8/src/board.c:75-77` | `++g_reset_count;` |
| nice_nano | `board_nice_nano_system_reset` | `board/nice_nano/src/board.c:66-68` | `++g_reset_count;` |
| rpi_pico | `board_rpi_pico_enter_low_power` | `board/rpi_pico/src/board.c:44-45` | `++g_low_power_entries;` |

**但有一个提案没说、而我认为应当计入判定的事实**：这些计数器是**故意暴露给测试的**。每个板都导出了
对应的 getter（`board/blackpill/include/blackpill/board.h:44` 的 `board_blackpill_low_power_entries()`、
`:46` 的 `board_blackpill_reset_count()`），也在 `hw_init` 里清零（`board/blackpill/src/board.c:15`、`:17`）。
换句话说：**这是有意设计的主机模拟探针，不是忘了写硬件操作**。同类还有 `board_*_feed_watchdog()`
（`blackpill:62-64` 也是 `++g_watchdog_feeds;`）。

所以问题的重心恰好落在提案最后那句话上：**不是“实现错了”，而是“两种定位没在文档里区分”**。
一个叫 `board_blackpill_system_reset()` 的函数返回后“什么都没重置”，只因它是个宿主探针。
这与 §六 是同一类问题（名字与注释暗示硬件，行为是内存）；§六 的那次搜索（`docs/` 下
`register model` / `mmio` / `host model` / `simulat` 全部无命中）同样适用于这里 —— 仓库里没有任何一处
声明「这些板是 host simulation profile」。

**判定：成立**（事实成立；其「必须在文档中明确两种定位」的要求也成立）。

## 十、CI 「只 build 不 run」—— **成立**（同时也是 §一 的成因）

`product-matrix` 这个 job 只有两步与产品有关：

- `.github/workflows/ci.yml:199-205`：`Build all legal products` → `cmake --build build --parallel`
  —— **构建所有合法产品**。
- `.github/workflows/ci.yml:206-211`：`Run host products` →
  `for product in example meter_host meter_mps2 meter_gateway_host; do … ./build/$product; done`
  —— **只运行硬编码的四个**。

于是 `pinetime`、`watch_host`、`keyboard_host`、`corne_split` 这类产品 **编得过、从不执行**。
这正是 §一 里「PineTime 漏掉大量 init 而 CI 全绿」能够长期成立的直接原因：
`init` 缺失是运行时后果，而运行时从未发生。

同一 job 还包含了负向组合矩阵（`:213-225` 起，对 `family/board/app/infra/combo/rebind/duplicate`
逐个断言配置应当失败）—— 说明**配置期不变量}已经被 CI 兜住了，**运行期不变量没有**。
提案建议的 `product-runtime` job（构造 → init → start → event → poll → shutdown 跑一遍）
是成本最低的堵法。

**判定：成立**。
---

## 十一、clang-tidy 的盲区 —— **成立**

`ci.yml:396-416` 的 clang-tidy 步骤就是把文件一个一个写上去的（共 **18 个**）：

```text
edge_module/src/event.c          sys/runtime/src/sys.c        sys/example/sys_example.c
sys/meter/sys_meter.c            app/dlt645/src/dlt645.c      app/relay/src/relay.c
app/modbus_slave/src/modbus_slave.c  app/meter_core/src/meter_core.c
infra/flash/src/flash.c          infra/gpio/src/gpio.c        infra/uart/src/uart.c
board/example/src/board.c        board/mps2/src/board.c       pal/host/src/host.c
pal/os/src/idle.c                pal/os/src/tick64.c          pal/eos/src/eos.c
pal/cortex-m-bare/src/pal_cortex_m.c
```

与提案列的「没有」逐项对得上：**没有** `soc/rp2040`、**没有** `soc/stm32f4`、**没有** `app/zmk_*`、
**没有** `app/ble_*`、**没有** `product/pinetime`、**没有** `board/{blackpill,nice_nano,rpi_pico,colmi_p8}`。
而这次更新的重点（§六 的 soc、§二/三/四/五 的 zmk、§一 的 pinetime）恰好全在这个白名单之外。

`cppcheck` 的覆盖面确实不一样（`ci.yml:417-440`）：它显式排除 `-i tests/guards`，再挂上整棵树的
include 路径，而不是一份逐文件清单。两者的差别就在于：**新目录不会自动被 clang-tidy 看到**。

我也认同提案的修法：既然同一 job 已经打开了 `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`（`:386`），
就从 `compile_commands.json` 取 translation units，而不是维护白名单。

**判定：成立**。

## 十二、CodeQL 不覆盖产品运行时 —— **不成立**（措辞过宽；真实关切已在 §十）

提案引的两行属实，`codeql.yml:36-40` 确实是：

```yaml
cmake -S . -B build-codeql -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DEDGE_MODULE_BUILD_TESTS=OFF \
  -DEDGE_MODULE_BUILD_EXAMPLE=ON
cmake --build build-codeql --parallel
```

但**从这两行推到「pinetime / ZMK 产品 / 新 board 运行时不在 CodeQL 的执行路径上」是错的**：
`EDGE_MODULE_BUILD_EXAMPLE=ON` ＋ `cmake --build` 会构建默认目标，而 `product-matrix` job
用的就是**同一组开关**（`ci.yml:199-205`，同样 `BUILD_TESTS=ON` + `BUILD_EXAMPLE=ON`），
紧接着就能直接 `./build/$product`（`:206-211`）—— 那些可执行文件之所以存在，正是因为这种配置
就把它们构建了出来。因此 CodeQL 的构建实际上也编了这些产品，**建议里增加的 target 不会改变
被分析的 TU 集合**。

另外要公平地说：CodeQL 是**静态**分析，它不可能发现「init 没被调用」这类运行时语义缺陷。
提案真正想说的「PineTime 漏 init 而不被任何岗哨拦住」已经是 §十 的结论（产品**被构建但从不执行**），
把它归给 CodeQL 矩阵会指向错误的修法。

**判定：不成立**（作为对 CodeQL 构建矩阵的断言，它是过宽的；背后那个真问题在 §十 已判定为成立）。

---

## 十三、Part 3（`proposal.md` 3060 行起）：一条前提成立，一个缺陷指控不成立

### 13.1 「当前的中断架构是 ISR → event queue」—— **成立**

提案引的三句话逐字存在于 ADR 里，且是**可执行强制**的：

- `docs/adr.md:37`：`D3 | board module 范围 | **只转发中断**，不做设备访问`
- `docs/adr.md:51`：`D17 | 事件路由 | **board 只 push 到注入的 sink**`，并注明「board 不认识 app（守 D3）」
- `docs/adr.md:798`：`board/ISR 只 push 到注入的 sink；sys 按 event id 路由（D17）`
- `docs/adr.md:1156`：`ISR 只 push；runner 只 pop`

而「board 不认识 app」不是口号：`.github/scripts/check_layer_dependencies.py:29` 把 board 的允许集写成
`{"board", "soc", "pal", "edge_module"}`，`app` 不在里面；该脚本的 docstring（`:6-7`）就是把这些关系
**做成可执行而不是散文**。所以提案说的「当前架构基本就是这么定义的」是准确的。

### 13.2 「UART RX 每字一 event 造成事件风暴」—— **不成立**（作为对现有代码的断言）

这一段里只有那段 `ISR() { … edge_event_queue_push(…) }` 是具体的，而它自己在开头就写了「**假设** UART
115200 baud」。实测当前代码里根本还没有这条路：

- `git ls-tree -r --name-only f2f3da10 -- pal/ | grep -i uart` → **无**；`pal/ soc/ board` 下搜
  `uart_rx|UART_RX|rx_isr|on_rx` → **无命中**。
- 真的 UART 路径在 `infra/`：`infra/uart/include/uart/uart.h:20-21` 只有
  `edge_status_t uart_write(void *self, const void *buf, size_t len);` 与
  `uart_read(void *self, void *buf, size_t len);`
- `infra/uart/` 与 `app/dlt645/` 下搜 `queue_push|event_queue|EDGE_EVT` → **无命中**。

也就是说：当前是**拉取式端口**，既不是「每字一 event」也不是任何 ISR 路径；§3 的批评在今天的代码上
没有对象。（它是**前瞻性**的：J 阶段把 ChibiOS 引进来、真 ISR 出现之后，这条警告才变得可验证 ——
所以我把 `uart.h:20-21` 这个现状当作后续重构的基线记在这里。）

### 13.3 其余段落（§4 DMA / §5 Timer / §6 HardFault / §7 四类路径 / §10 EventQueue 作为控制面）

属**设计主张**，不是可核验断言：它们描述的是「应该怎么做」，而仓库现在还没有这些 ISR 实现
（同 13.2 的搜索）。提案里可被证伪的部分（前提、现状）已在 13.1/13.2 处理；剩下的取舍按其自己的
Phase 1–3 推进即可，本文不当作缺陷计分。

---

## 严重度排序（本文最终的清单）

**A 级：静默失效 + 岗哨看不见**

1. **§一 PineTime 21 个 module 未 `init`（含 `required_ids` 里的 `watch_ui`）** —— 告警/设置读不回、
   一个必需模块的初始化端口从未接上；而 CI 只构建、从不运行它（见 2）。
2. **§十 CI 只 build 不 run** —— 产品矩阵只跑硬编码四个，这是 A1、A2 及同类问题能长期潜伏的**结构成因**。

**B 级：安全与数据一致性**

3. **§四 Studio `UNLOCK_DEVICE` 无任何前提** —— 锁的作用域是真的，钥匙是白送的（USB/BLE 皆可）。
4. **§五 RPC 吞错，且失败时还清掉 `unsaved_changes`** —— 客户端与设备**双双**以为已保存。

**C 级：真硬件上不可用（但有意的模型）**

5. **§六 + §八 SoC 是 RAM/寄存器模型**，SPI 写后回读、I2C 直接丢弃从机地址（`soc_stm32f4.c:95/:106`）。
6. **§七 RP2040 `wdt_feed` 不重装 `load`**，而参考同一个文件的 `wdt_start` 会重装。
7. **§九 板级 `system_reset` / `low_power` 是计数器探针** —— 实现是有意的，**缺的是文档定位**。

**D 级：局部语义 / 过程改进**

8. **§二 hold-tap 开机窗口内首击误入 quick-tap** —— 长按会被吞（窗口仅开机后 `quick_tap_ms`）。
9. **§三 macro 的 `WAIT`/`wait_ms`/`default_wait_ms` 是空壳**，而同一个仓里的 `behavior_queue` 已实现同类能力。
10. **§十一 clang-tidy 是 18 文件手工白名单** —— 新增目录（soc/zmk/pinetime/新板）默认不被审查；
    同 job 已有 `compile_commands.json`（`ci.yml:386`）可用。

**不成立（已证伪，不计入）**

- **§十二 CodeQL 构建矩阵不覆盖产品** —— 同名开关同样会构建出产品二进制（`ci.yml:199-211`）；
  真问题已在 §十 计分。

**本文额外发现（提案未提）**

- `watch_ui` 已在 `required_ids` 且被订阅四个事件，但 `watch_ui_init` 从未被调用（§一）。
- `save_changes` 失败时 `unsaved_changes` 仍被清成 `false`，连 `GET_UNSAVED` 都回报 0（§五）。
- `board/blackpill/src/board.c:55` 的 `delay_ms * 2000` 疑似把超时算成 2 倍（**待外部数据手册核实**，未计入判定）。

---

## 核验方法与局限

- 基准：`f2f3da10`（= `main` 的 HEAD），用 `git show f2f3da10:<path>` / `git grep … f2f3da10` 读**当时**的代码，
  而不是拿当前工作分支（`bldc`，VESC 迁移）上不存在的文件作反证。
- 本文只对**可核验事实**给判定；设计主张（Part 2 全部、Part 3 的 §4—§10）不参与计分，只作为方向参考。
- 所有命令与行号都写在正文里，可重跑；凡我没能核实的（如 §七 的 `×2000` 倍数）都标为线索而非判定。
