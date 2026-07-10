## ADDED Requirements

### Requirement: 开机 Wi-Fi 引导来源选择
在 `CONFIG_OPENTHREAD_BR_AUTO_START` 与 `CONFIG_EXAMPLE_CONNECT_WIFI` 启用时，BR SHALL 在开机时按以下优先级确定 Wi-Fi 凭据来源：(1) 若 NVS 存有凭据，使用 NVS；(2) 否则，若 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP` 启用，进入 SoftAP 配网模式等待用户提交；(3) 否则使用编译期预设 `CONFIG_EXAMPLE_WIFI_SSID` / `CONFIG_EXAMPLE_WIFI_PASSWORD`。

#### Scenario: NVS 存有凭据
- **WHEN** 开机时 NVS 中存在 Wi-Fi SSID/密码
- **THEN** BR 使用 NVS 凭据连接，不进入 SoftAP，也不使用编译期预设

#### Scenario: 空 NVS 且启用 SoftAP 配网
- **WHEN** NVS 无凭据且 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP` 启用
- **THEN** BR 启动 SoftAP 与配网 Web 服务，等待用户提交凭据

#### Scenario: 空 NVS 且未启用 SoftAP 配网
- **WHEN** NVS 无凭据且 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP` 未启用
- **THEN** BR 使用编译期预设 SSID/密码连接

### Requirement: SoftAP 配网结束后干净初始化 STA
用户经 SoftAP 配网页提交凭据后，BR SHALL 使 Wi-Fi driver 与 STA 初始化状态保持一致，随后完成 STA 初始化（含创建 STA netif）并尝试连接目标网络。BR SHALL NOT 因 Wi-Fi driver 的重复初始化（`ESP_ERR_INVALID_STATE`）而 abort。

#### Scenario: 空 NVS 开机配网后连接
- **WHEN** STA 侧从未初始化（STA netif `WIFI_STA_DEF` 不存在），用户经 SoftAP 配网页提交凭据后 SoftAP 停止
- **THEN** BR 将 Wi-Fi driver 反初始化以对齐状态，随后完整初始化 STA（创建 STA netif）并连接用户提交的网络，不 abort

#### Scenario: 不因重复初始化崩溃
- **WHEN** SoftAP 启动时已调用 `esp_wifi_init` 使 driver 处于已初始化状态
- **THEN** 后续 STA 初始化路径不因 `esp_wifi_init` 返回 `ESP_ERR_INVALID_STATE` 触发 `ESP_ERROR_CHECK` abort

#### Scenario: CLI 手动配网路径不劣化
- **WHEN** STA 侧已初始化（STA netif `WIFI_STA_DEF` 已存在，如先经 `wifi connect` 命令），此后进入并退出 SoftAP 配网
- **THEN** 退出 SoftAP 时保持 STA 模式而不反初始化，后续连接不因 `ESP_ERR_WIFI_NOT_INIT` 失败
