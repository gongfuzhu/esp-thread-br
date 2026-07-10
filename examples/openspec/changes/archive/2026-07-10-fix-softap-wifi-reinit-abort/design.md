## Context

`OPENTHREAD_BR_SOFTAP_SETUP=y` 且空 NVS 时，开机配网在用户网页提交凭据后 abort：

```
E wifi_init: Failed to init, WiFi is initialized by esp_wifi_init
ESP_ERROR_CHECK failed: ESP_ERR_INVALID_STATE at example_wifi_start (wifi_connect.c:121)
```

涉及三处代码：
- `examples/common/thread_border_router/src/border_router_launch.c` — `ot_br_init` 任务的 Wi-Fi 引导决策（109-142 行）。空 NVS + SoftAP 分支：`esp_br_wifi_config_start()` → 等待网页提交 → `esp_br_wifi_config_stop()` → `wifi_config_save_and_connect()` → `esp_ot_wifi_connect()`。
- `components/esp_ot_br_server/src/esp_br_wifi_config.c` — SoftAP 侧。`wifi_config_start_softap()`（249 行）自己调 `esp_wifi_init()`，且容忍 `ESP_ERR_INVALID_STATE`；`wifi_config_stop_softap()`（267 行）**故意不 deinit**，只 `esp_wifi_stop()` + `set_mode(STA)` + `esp_wifi_start()`，并 destroy AP netif。
- `components/esp_ot_cli_extension/src/esp_ot_wifi_cmd.c` — STA 侧。`esp_ot_wifi_connect()`（186 行）用静态标志 `s_wifi_initialized` 守卫 `example_wifi_start()`（IDF 代码，内部含 `esp_wifi_init` + 创建 STA netif + set_mode/start）。

约束：不改 IDF 代码；无主机测试；`s_wifi_initialized` 是 `esp_ot_wifi_cmd.c` 私有静态变量，无对外接口。

## Goals / Non-Goals

**Goals:**
- 开机自动配网路径（空 NVS + SoftAP + 网页提交）走完后能连上目标 Wi-Fi，不 abort。
- 让「IDF Wi-Fi driver 初始化状态」与「STA 侧 `s_wifi_initialized` 标志」重新对齐。
- CLI 手动配网路径（`wifi connect`）行为不劣化。

**Non-Goals:**
- 不修改 IDF `example_wifi_start`/`wifi_connect.c`。
- 不改变 `SOFTAP_SETUP=n` 时的行为（编译期预设路径）。
- 不重构 Wi-Fi 状态管理为单一状态机（超范围）。
- 不新增 SoftAP 之外的配网方式。

## Decisions

**决策 1：在 `wifi_config_stop_softap()` 按 STA 初始化状态对齐 driver 状态。**
根因是两个初始化标志语义错位：`esp_wifi_init` 表达「driver 已初始化」，`s_wifi_initialized` 表达「STA 侧（含 STA netif、coex、地址 handler）已初始化」。SoftAP 的 `esp_wifi_init` 只推进了前者，后者仍 false，导致 `esp_ot_wifi_connect` 再次 init 撞 `INVALID_STATE`。

修复：`wifi_config_stop_softap()` 依据 STA 是否已初始化分支——
- STA **未**初始化（`s_wifi_initialized==false`，开机自动配网）→ `esp_wifi_deinit()` 使 driver 归零，**删除**现有的 `set_mode(STA)+start` 半吊子操作。之后 `esp_ot_wifi_connect` 走完整 `example_wifi_start`，干净创建 STA netif 并连接。
- STA **已**初始化（`s_wifi_initialized==true`，CLI 手动配网）→ 保持现有 `set_mode(STA)+esp_wifi_start()`，不 deinit（正是原注释所防的 `NOT_INIT` 场景）。

- 备选（拒绝）：**方案 A**——在 `esp_ot_wifi_connect` 侧探测 driver 状态、跳过 `esp_wifi_init`。但 `example_wifi_start` 是打包函数，跳过 init 会连带跳过 STA netif 创建，而 SoftAP 建的是 AP netif、且 stop 时已 destroy，STA netif 从不存在——拆包需改 IDF 代码或重写该逻辑，代价更大。
- 备选（拒绝）：**方案 C 无条件 deinit**——会在 CLI 路径（STA 已 init）触发原作者注释警告的 `ESP_ERR_WIFI_NOT_INIT`。

**决策 2：以本地 STA netif 判定 STA 初始化状态，而非跨组件读取 `s_wifi_initialized`。**
`wifi_config_stop_softap()` 需要知道 STA 侧是否已初始化。最初设计是在 `esp_ot_cli_extension` 新增只读 getter `esp_ot_wifi_is_sta_initialized()` 暴露 `s_wifi_initialized`；但编译暴露：`basic_thread_border_router` 经组件注册表（`idf_component.yml` → `espressif/esp_ot_cli_extension ~2.0.0` → `managed_components/`）解析该组件，仓库本地 `components/esp_ot_cli_extension/` 新增的 getter 对构建**不可见**，`esp_br_wifi_config.c`（本地 path 依赖，会编译）引用该符号导致隐式声明报错。

改为在 `esp_br_wifi_config.c` 内部用 `esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")` 本地判定：STA netif 仅由 `example_wifi_start()` 创建，SoftAP 路径只建 AP netif（stop 时 destroy），因此「STA netif 存在」与「STA 侧已初始化（`s_wifi_initialized==true`）」语义等价。`esp_netif.h` 已包含，零新增依赖，改动收敛到单文件。此 ifkey 正是连接流程里 `handle_wifi_addr_init()` 已在用的键（见决策 3），一致性有保证。

**决策 3：`handle_wifi_addr_init` 的 IP6 handler 依赖 STA netif 存在。**
`handle_wifi_addr_init` 用 `esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")` 注册 IP6 事件——该 handle 仅在 `example_wifi_start` 创建 STA netif 后存在。这佐证「让开机路径走完整 init」是正确方向：deinit 归零后由 `example_wifi_start` 从头建 STA netif，handler 才能正确绑定。

## Risks / Trade-offs

- [CLI 手动配网路径未硬件复现] → else 分支保持作者原逻辑不变，基于代码审查保证不劣化；用户终端不支持 CLI 转义序列，无法实测。缓解：修复只在 `s_wifi_initialized==false` 时改变行为（deinit），`==true` 分支字节级保持原样。
- [deinit 遗漏对称清理] → 原作者曾因只处理一半状态（保 driver 未同步标志）留下此 bug。本次 deinit 前需确认 AP 事件 handler 已 unregister（现有 `wifi_config_stop_softap` 开头已做）、AP netif 已 destroy（现有已做），避免残留。
- [无自动化测试] → 仅能 `idf.py build` + 设备端 monitor 验证；主验证开机自动配网路径。

## Migration Plan

1. `esp_br_wifi_config.c`：改 `wifi_config_stop_softap()` 按 STA netif 是否存在分支（`esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")`）。单文件改动，无新增依赖。
2. `idf.py build`（用户在 EIM/PowerShell 环境）。
3. 临时置 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP=y` + erase-flash 空 NVS，开机走配网页提交凭据，确认不再 abort 且联网成功。
- 回滚：还原 `esp_br_wifi_config.c`；`SOFTAP_SETUP=n` 默认路径本就不受影响。

## Open Questions

- CLI 手动配网路径（`wifi connect` 后再进 SoftAP）是否值得后续用支持转义序列的终端补一次实测，以彻底闭环 else 分支？当前判断：非阻塞，代码审查足够。
