## 1. 嵌入 CA 证书

- [x] 1.1 新建 `common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`，写入 DigiCert Global Root G2 PEM
- [x] 1.2 修改 `common/mqtt_ot_bridge/CMakeLists.txt`：在 `idf_component_register` 增加 `EMBED_TXTFILES "certs/mqtt_ca_cert.pem"`

## 2. 组件 TLS 支持

- [x] 2.1 在 `mqtt_ot_bridge.c` 顶部声明 `extern const uint8_t _binary_mqtt_ca_cert_pem_start[]`（及 `_end`）
- [x] 2.2 在 `mqtt_ot_bridge_start()` 的 `esp_mqtt_client_config_t` 增加 `.broker.verification.certificate`（指向嵌入的 CA PEM）
- [x] 2.3 确认 `mqtt://` 明文场景不受影响（CA 挂上但 scheme 决定是否 TLS）

## 3. 更新连接参数

- [x] 3.1 在 `basic_thread_border_router/sdkconfig.defaults` 覆盖 `CONFIG_MQTT_OT_BRIDGE_BROKER_URI="mqtts://ibf71a0f.ala.cn-hangzhou.emqxsl.cn:8883"`
- [x] 3.2 覆盖 `CONFIG_MQTT_OT_BRIDGE_USERNAME="gfz"` 与 `CONFIG_MQTT_OT_BRIDGE_PASSWORD="QWERasdf"`

## 4. 文档

- [x] 4.1 更新 `common/mqtt_ot_bridge/Kconfig.projbuild` 帮助文本，去掉「不使用 TLS」，说明支持 `mqtts://` + CA 校验
- [x] 4.2 更新 `common/mqtt_ot_bridge/README.md` 相应描述

## 5. 验证

- [x] 5.1 `idf.py build` 编译通过（用户在 EIM/PowerShell 环境完成；bash 侧 IDF 环境不可激活）
- [x] 5.2 `idf.py -p <PORT> flash monitor`：设备连上 Wi-Fi 后 MQTT over TLS 握手成功、连接 broker（用户硬件验证通过）

## 6. 硬件验证阶段发现并修复的额外问题

- [x] 6.1 **BR 因 MQTT 启动失败而 abort**：`esp_ot_br.c:111` 原为 `ESP_ERROR_CHECK(mqtt_ot_bridge_start())`，MQTT 初始化失败时 abort 整个 BR，违反「MQTT 失败不影响 Thread/BR」的 spec。改为记 `ESP_LOGE` 后继续（commit 6bbfcbf）。
- [x] 6.2 **SoftAP 配网屏蔽预设 Wi-Fi**：`CONFIG_OPENTHREAD_BR_SOFTAP_SETUP=y` 与 `CONFIG_EXAMPLE_WIFI_SSID` 在 `border_router_launch.c` 的 `ot_br_init` 中是编译期互斥——开启 SoftAP 时空 NVS 会永久停在配网页，不回落预设 SSID，导致无外网、broker DNS 解析失败（`getaddrinfo 202`）。且配网页路径本身在二次 `esp_wifi_init` 处 abort。改为 `CONFIG_OPENTHREAD_BR_SOFTAP_SETUP` is not set，使空 NVS 设备用预设 Wi-Fi 自动连（commit 08dc4d5）。
