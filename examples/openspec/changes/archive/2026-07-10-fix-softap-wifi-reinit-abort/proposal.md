## Why

启用 SoftAP 配网（`CONFIG_OPENTHREAD_BR_SOFTAP_SETUP=y`）且 NVS 无 Wi-Fi 凭据时，设备开机进入 SoftAP 配网页；用户通过网页提交 SSID/密码后，Border Router **直接 abort 崩溃**：

```
E wifi_init: Failed to init, WiFi is initialized by esp_wifi_init
ESP_ERROR_CHECK failed: ESP_ERR_INVALID_STATE at example_wifi_start (wifi_connect.c:121)
```

根因是两个「Wi-Fi 已初始化」状态语义错位：SoftAP 启动时（`esp_br_wifi_config.c` 的 `wifi_config_start_softap`）自己调了 `esp_wifi_init()`，使 IDF Wi-Fi driver 进入已初始化状态，但 `esp_ot_cli_extension` 的静态标志 `s_wifi_initialized` 仍为 `false`。配网结束后 `esp_ot_wifi_connect()` 见标志为 false，再次调用 `example_wifi_start()` → 其中的 `esp_wifi_init()` 撞上 `ESP_ERR_INVALID_STATE` → `ESP_ERROR_CHECK` abort。结果：**开启 SoftAP 配网的设备根本无法完成配网联网**，只能靠关闭该特性、走编译期预设 Wi-Fi 绕过。

## What Changes

- 修复 `wifi_config_stop_softap()`（`components/esp_ot_br_server/src/esp_br_wifi_config.c`）：使其根据 STA netif 是否存在，决定停 SoftAP 后是 `esp_wifi_deinit()`（让 driver 状态归零）还是保持现有的 `set_mode(STA)+start` 行为，从而与 STA 侧初始化状态对齐。
- STA 初始化状态在 `esp_br_wifi_config.c` 内部通过 `esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")` 本地判定（STA netif 仅由 `example_wifi_start` 创建，SoftAP 只建 AP netif），**不引入对 `esp_ot_cli_extension` 的跨组件依赖**——该组件在示例中经组件注册表解析（`managed_components/`），仓库本地新增的 getter 对构建不可见。
- 结果：开机自动配网路径（空 NVS + SoftAP）走完配网后能干净地从头初始化 STA、创建 STA netif 并连上目标 Wi-Fi，不再 abort；CLI 手动配网路径（`wifi connect`）行为保持不变。

说明：本次**不改 IDF 代码**（`example_wifi_start` 保持原样）；不改动 `esp_wifi_init` 在 SoftAP 侧已容忍 `INVALID_STATE` 的现有逻辑（那一侧是对的）。CLI 手动配网路径因终端限制未做硬件复现，靠代码审查保证不劣化（见 design 风险）。

## Capabilities

### New Capabilities
- `border-router-wifi-provisioning`: 定义 BR 开机 Wi-Fi 引导行为——NVS 凭据、编译期预设、SoftAP 配网三种来源之间的选择与状态机，以及配网结束后必须能干净初始化 STA 并联网、不得 abort。

### Modified Capabilities
<!-- 无现有能力的 requirement 变更 -->

## Impact

- 代码：仅 `components/esp_ot_br_server/src/esp_br_wifi_config.c`（`wifi_config_stop_softap`）单文件改动。
- 行为：仅影响 `OPENTHREAD_BR_SOFTAP_SETUP=y` 时的开机配网路径；`SOFTAP_SETUP=n`（当前 `basic_thread_border_router` 默认）不受影响。
- 不新增依赖；不改分区/sdkconfig。
- 验证：无主机测试，靠 `idf.py build` + 设备端 monitor。主验证用例为开机自动配网（`SOFTAP_SETUP=y` + 空 NVS + 网页提交），确认不再 abort 且能联网。
