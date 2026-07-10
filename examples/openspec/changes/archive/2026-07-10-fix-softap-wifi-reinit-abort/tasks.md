## 1. 对齐 stop_softap 的 Wi-Fi driver 状态（本地 STA netif 判定）

- [x] 1.1 在 `components/esp_ot_br_server/src/esp_br_wifi_config.c` 的 `wifi_config_stop_softap()` 中，以 `esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")` 是否为 `NULL` 判断 STA 是否已初始化（STA netif 仅由 `example_wifi_start` 创建；SoftAP 只建 AP netif）。`esp_netif.h` 已包含，无需新增依赖或 include。
- [x] 1.2 STA netif 不存在分支（开机自动配网）：`esp_wifi_deinit()` 使 driver 归零，删除原有 `esp_wifi_set_mode(WIFI_MODE_STA)` + `esp_wifi_start()` 半吊子操作。
- [x] 1.3 STA netif 存在分支（CLI `wifi connect`）：保持原有 `esp_wifi_set_mode(WIFI_MODE_STA)` + `esp_wifi_start()` 不变。
- [x] 1.4 `esp_wifi_stop()` 仍在分支之前；AP 事件 handler unregister、AP netif destroy 在两分支之后照旧执行。

> 放弃方案：曾尝试在 `esp_ot_cli_extension` 新增 getter `esp_ot_wifi_is_sta_initialized()` 跨组件查询 `s_wifi_initialized`，但该示例经组件注册表解析 `esp_ot_cli_extension`（`managed_components/`），仓库本地新增的 getter 对构建不可见，导致 `esp_br_wifi_config.c` 隐式声明编译失败。改为本地 STA netif 判定，语义等价且零跨组件依赖。

## 2. 编译

- [x] 2.1 `idf.py build` 编译通过（用户 EIM/PowerShell 环境），无未定义引用、无头文件缺失。

## 3. 设备验证（开机自动配网路径）

- [x] 3.1 临时设 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP=y`，`erase-flash` 清空 NVS 后 `flash monitor`。
- [x] 3.2 开机进入 SoftAP，连热点访问 `http://192.168.4.1` 提交 SSID/密码。
- [x] 3.3 确认提交后**不再 abort**（无 `esp_wifi_init` / `INVALID_STATE` panic），设备连上目标 Wi-Fi 并拿到 IP。用户实测通过。
- [x] 3.4 验证后将 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP` 恢复为默认（not set），确认默认预设路径仍正常。
