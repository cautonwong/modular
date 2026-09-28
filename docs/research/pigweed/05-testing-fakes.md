# 轴五：host 侧测试与 fake / backend 测试惯例（pigweed → modular）

只读调研。pigweed 路径相对 `/workspaces/vendor/pigweed`，我们的路径相对 `/workspaces/vendor/modular`。

## 0. 读了哪些路径

- pigweed 测试框架：`pw_unit_test/{test.gni, test.cmake, BUILD.gn, docs.rst}`
- pigweed 替身：`pw_digital_io/{digital_io_mock.cc, public/.../digital_io_mock.h, digital_io_mock_test.cc, BUILD.gn, CMakeLists.txt}`、
  `pw_i2c/public/pw_i2c/initiator_mock.h`、`pw_rpc/public/pw_rpc/{internal/fake_channel_output.h, test_helpers.h}`、
  `pw_chrono/public/pw_chrono/simulated_system_clock.h`、`pw_compilation_testing/public/.../negative_compilation.h`、
  `pw_system/{socket_target_io.cc, stl_backends.gni}`
- 我们：`tests/CMakeLists.txt`、`tests/contract/port_contract.h`、`tests/test_foc_core.c`、`tests/test_infra_flash_emul.c`、
  `tests/test_product_glue.c`、`tests/test_app_relay.c`、`cmake/EdgeTargets.cmake`、`product/vesc_host/main.c`、`docs/bldc-migration.md`

## 1. pigweed 的测试骨架（有证据）

### 1.1 `pw_unit_test` 是 facade，「一份测试源码，host 与 target 都能编」
- 测试只 include `pw_unit_test/framework.h`，真实实现由 backend 决定：`pw_unit_test/BUILD.gn:71-74`
  （`public_deps = [ ..., pw_unit_test_BACKEND ]`）。
- 默认 backend 是设备友好的 `light`（GoogleTest 子集），host 可切 `googletest`：
  `pw_unit_test/test.gni:31`（`pw_unit_test_BACKEND = "$dir_pw_unit_test:light"`）、
  `pw_unit_test/docs.rst:263-267`（“lets you write unit tests once and run them under many different environments”）。
- 同一份测试源码在 CMake 里被拆成对象库+可执行+运行目标，并可按组打包成设备 bundle：
  `pw_unit_test/test.cmake` 的 `pw_add_test` 产生 `{NAME}.lib` / `{NAME}.bin` / `{NAME}.run`，
  `pw_add_test_group` 再把多个 `.lib` 链成一个 `{NAME}.bundle.lib`（可链进 on-device 测试二进制）。
  这解释了「`.lib`/`.bin` 拆分」为什么存在：host 跑 `.run`，设备跑同一份 `.lib` 链出来的 bundle。
- 运行器可替换：`pw_unit_test/test.gni:53`（`pw_unit_test_AUTOMATIC_RUNNER`）、
  `pw_unit_test/test.cmake`（`pw_unit_test_AUTOMATIC_RUNNER` + 超时/参数）。设备 runner 只需换这一个变量。
- 设备侧还提供 RPC 通道来收集结果（`pw_unit_test/docs.rst:620-645`），host 侧的联调传输在
  `pw_system/socket_target_io.cc` / `pw_system/stl_backends.gni`。

### 1.2 替身是**独立构建目标**，住在模块目录里，不住在测试文件里
- `pw_digital_io/BUILD.gn:65-70` 单独声明 `pw_source_set("digital_io_mock")`（`sources = [digital_io_mock.cc]`,
  `public = [public/.../digital_io_mock.h]`）；CMake 侧对应 `pw_digital_io/CMakeLists.txt:37-49`
  （`pw_add_library(pw_digital_io.digital_io_mock STATIC ...)`）。
- 于是任何模块（含另一个模块的测试）都能 `deps = [":digital_io_mock"]` 复用，而不是各自抄一份。
- 命名两族：`*_mock`（带期望、会判对错，如 `pw_i2c/.../initiator_mock.h:174` 的 `MockInitiator` +
  `:187 Finalize()`）与 `*_fake`（只记录、不判对错，如 `pw_rpc/.../internal/fake_channel_output.h:49`
  `FakeChannelOutput` 存下所有出包）。二者都放 `public/` 头里。

### 1.3 注入方式：构造期拿接口引用 + 调用方拥有的事件缓冲 + 可注入时钟
- `pw_digital_io/public/pw_digital_io/digital_io_mock.h:31-54`：`DigitalInOutMockImpl` 的构造函数拿
  `Clock&` 与 `pw::InlineDeque<Event>&`；`:57-68` 的模板 `DigitalInOutMock<kCapacity>` 自己拥有那块 deque
  ——**缓冲由调用方/模板拥有，替身不分配内存**。
- `digital_io_mock.cc:31-44`：`DoSetState` 把 `{clock_.now(), state}` 记进队列，host 上不点灯。
- 时间可控：`pw_chrono/public/pw_chrono/simulated_system_clock.h:41`（`SimulatedSystemClock : VirtualSystemClock`），
  替身默认 `Clock::RealClock()`（`digital_io_mock.h:62`），测试可换成模拟钟。
- 异步/RPC 用回调钩子同步：`pw_rpc/public/pw_rpc/test_helpers.h:33-45` 用 `output.set_on_send(...)` + 计数信号量
  把「等 N 个包」变成确定性等待，而不是 sleep。

### 1.4 替身**自己也被测**
- `pw_digital_io/BUILD.gn:79-83` 有 `pw_test("digital_io_mock_test")`；
  `pw_i2c/initiator_mock_test.cc`、`pw_rpc/fake_channel_output_test.cc` 同理。
- 理由很清楚：替身是别的测试的「真值来源」，它错了会让一堆测试假绿。

### 1.5 负面编译测试
- `pw_compilation_testing/public/.../negative_compilation.h:16-38`：`#if PW_NC_TEST(名称)` +
  `PW_NC_EXPECT("正则")`，用宏开关把「这段必须编不过」写进正常测试文件；未启用时展开成 `0 && ...`。

## 2. 我们这边现状（对照）

- 测试入口是自报依赖的 helper：`cmake/EdgeTargets.cmake:107-123` 的 `edge_add_host_test`（`add_executable` +
  `add_test` + `LABELS host`），`tests/CMakeLists.txt:1-33` 用它逐个声明。
- 已有「共享的行为规范」基建：`tests/contract/port_contract.h:1-27` 定义端口形状，任何实现（glue/驱动/测试 fake）
  都跑同一套断言（D57）；`tests/CMakeLists.txt:36-48` 还把每个 contract 套件对着一个**故意写坏的实现**跑，
  用 CTest `WILL_FAIL TRUE` 证明套件真的会拒绝违规。
- 但替身是**测试文件内的 `static` 私有结构**：`tests/test_foc_core.c:16-23`（`mock_inverter_t`）、
  `tests/test_infra_flash_emul.c:26-38`（`mock_flash_t`，还注入掉电）、`tests/test_app_relay.c:14`（`fake_out_t`）。
  → 同一形状的 fake 在多个测试里各写一份，跨测试不可复用。
- `tests/test_product_glue.c:22-28`：为了覆盖率把产品适配器拉进来直接跑（我们最近的补覆盖率写法）。
- host 产品自检：`product/vesc_host/main.c:402-416`，跑完虚拟电机后用退出码 30/31 断言（不 grep stdout）。
- 差分 harness（我们没有独立脚本，证据在文档+黄金向量）：`docs/bldc-migration.md:314-330`，
  原版 `foc_math.c` 原样编译，同旗标、全量比对「逐位文本相同」；产物就是 `tests/test_foc_core.c:1008-1021` 的黄金向量。

## 3. 值得借 / 不适合我们

**值得借（便宜、C11 全兼容）**
1. 替身独立成可复用目标（`*_fake.c/.h` 放模块目录 + 一个 `add_library`），而不是每个测试自己写 `static`。
2. 替身自带测试（或挂进我们已有的 contract 套件），让「假端口的行为」也钉住。
3. 「记录型 fake + 可注入时钟」：我们的 app 单测现在靠固定 `dt` 调用，时间推进不能确定性控制。
4. 负面编译测试的宏化写法（`#if PW_NC_TEST(...)`）比 `negative_products.cmake`+`WILL_FAIL` 更细粒度，
   能定位到具体 `_Static_assert`/用法，而不是「整个产品编不过」。

**不适合照搬**
- `pw_unit_test` 的 facade + googletest backend：GoogleTest/GMock/C++17。我们是 cmocka + C11（D59），
  替换测试框架的代价远大于收益；contract 套件已经在扮演「一份行为规范多处跑」的角色。
- backend 变量间接层（`pw_add_backend_variable`，见 `pw_i2c/backend.cmake`）：我们用消费方定义的端口 +
  plain struct（D14/D22），已经更轻；再套一层 backend 变量只会增加一层跳转。
- GN/Bazel 的 `pw_test` 元数据与 `pw` 工具链矩阵：与我们的 CTest + `ci/*.json` 重复。
- `.lib/.bin` 拆分本身在纯 host 项目里是多余间接；只有当我们真要「同一份测试源码也编进 target 自检二进制」时才值得引入。

## 4. 我们的差分 harness 相对 pigweed 的优势

- pigweed 的替身是**行为近似**：`*_mock`/`*_fake` 只复刻接口语义（`digital_io_mock.h`、`fake_channel_output.h`），
  没有「真值」可比，所以它测不出「移植与参考差一个 ULP」这类偏差；它的负面编译测试也只能验编译期约束。
- 我们能**直接编参考固件**：`docs/bldc-migration.md:314-330` 把原版 `motor/foc_math.c` 原样编进来、同旗标全量比对
  「逐位文本相同」，产物冻结成 `tests/test_foc_core.c:1008-1021` 的黄金向量。pigweed 的 Bazel 环境做不到这件事——
  它的依赖是 `pw_*` 目标图，没有「外部固件源码原样编进 harness」这一位。
- 结论：替身的**组织方式**学 pigweed，替身的**可信度判据**留在我们自己的差分 harness。二者不冲突：
  fake 负责在 host 上把 app 的端口行为跑起来，黄金向量负责证明端口数值与参考一致。

## 5. 排序后的提案（≤5）

| # | 做什么 | 为什么适合我们 | 工作量 | 风险 |
|---|---|---|---|---|
| 1 | 把测试里的 `static` fake 提成模块自带的 `*_fake.c/.h` + 一个库目标（先做 `foc`、`flash`、`relay` 三个已有 2 处重写的） | 我们已有 contract 套件这个「共享规范」先例，只差把实现体也共享；C11 无成本，`test_*.c` 里 3 份 `mock_inverter` 形状会立刻收敛 | 小 | 低。命名要一次定死（`*_fake` 只记录、`*_mock` 会判对错），否则两种语义会混；`edge_enable_quality` 要挂到新库上，否则覆盖率数字会掉 |
| 2 | 新提的 fake 也挂进 `edge_contract_*_run`（或自带一个 `test_*_fake.c`） | 沿用 `tests/CMakeLists.txt:36-48` 的 `WILL_FAIL` 手法，成本极低；替身是别的测试的真值源，它错了会假绿一片 | 小 | 低。contract 套件现在按端口类型组织，port struct 与 app 自定义端口类型不同构，需要像 glue 一样写一层最小适配 |
| 3 | 给 app 单测引入「可注入时钟 + 记录型 fake」：把固定 `dt` 换成测试持有的 tick fake | 我们 app 的 period/timeout 逻辑现在只能按步数近似；pigweed 的 `VirtualSystemClock` 证明这条路径 host 端可行 | 中 | 中。要给每个 app 测试改调用点，且不能变成新的隐藏全局状态；建议只先在 1–2 个 app 上验证 |
| 4 | 用 `PW_NC_TEST` 那种宏化写法补 `_Static_assert` 的负面编译测试 | 现在 `_Static_assert` 只能靠「编译不过就失败」粗糙覆盖；宏化后能断言具体失败原因，和 `negative_products.cmake` 互补 | 中 | 中。C11 没有模板，可覆盖的失败点比 C++ 少（多为 `_Static_assert` + 错误用法）；需要给 CMake 加一个「预期编译失败」的 helper，别和 `WILL_FAIL`（运行期失败）混用 |
| 5 | 评估是否引入 `.lib/.bin` 拆分，让同一份测试源码也编进 target 自检二进制 | 对应 pigweed 的 host/target 双测；我们现在 host-only（`cmake/EdgeTargets.cmake:107`），target 侧只有产品自检 | 中 | 中–高。是构建系统的改动，收益取决于「我们有多少测试真能在 target 上跑」；在拿到 1–2 个已跑通的 target 测试前不建议动 |
