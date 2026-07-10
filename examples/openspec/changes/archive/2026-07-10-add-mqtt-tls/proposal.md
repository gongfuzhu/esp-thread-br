## Why

现有 MQTT 桥接只支持局域网明文 broker（`mqtt://host:port`，无 TLS）。实际部署需要把 BR 接入公网托管 broker（EMQX Cloud Serverless `ibf71a0f.ala.cn-hangzhou.emqxsl.cn`），该 broker 强制 TLS（8883 端口）并要求校验服务器证书。当前组件无任何 TLS/证书配置能力，连接会在 TLS 握手阶段直接失败。

## What Changes

- **BREAKING** `mqtt-bridge` 能力从「明文连接」放宽为「支持 TLS 连接并校验服务器证书」。默认配置由明文 broker 改为 `mqtts://` broker。
- 在 `mqtt_ot_bridge` 组件内嵌入 CA 证书（DigiCert Global Root G2，PEM），置于 `common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`，通过 `EMBED_TXTFILES` 打入固件。
- `mqtt_ot_bridge.c` 的 `esp_mqtt_client_config_t` 增加 `broker.verification.certificate`（指向嵌入的 CA PEM），使 `mqtts://` 连接完成证书校验。
- 更新 `basic_thread_border_router/sdkconfig.defaults` 的桥接连接参数：
  - `MQTT_OT_BRIDGE_BROKER_URI` → `mqtts://ibf71a0f.ala.cn-hangzhou.emqxsl.cn:8883`
  - `MQTT_OT_BRIDGE_USERNAME` → `gfz`
  - `MQTT_OT_BRIDGE_PASSWORD` → `QWERasdf`
- 更新组件 README / Kconfig 帮助文本，去掉「不使用 TLS」的措辞，说明支持 `mqtts://` 与 CA 校验。

说明：8084（WebSocket over TLS）仅供浏览器客户端使用，固件端走原生 MQTT-over-TLS 的 8883，不涉及。凭据 `QWERasdf` 与现有 WiFi 密码一样写入入库的 `sdkconfig.defaults`；如需脱敏可后续单独处理，不在本次范围。

## Capabilities

### New Capabilities
<!-- 无新增能力 -->

### Modified Capabilities
- `mqtt-bridge`: 「MQTT 客户端连接与登陆」requirement 从「不使用 TLS」改为「支持 TLS（`mqtts://`）并校验服务器证书」，连接参数仍来自 Kconfig，并新增 CA 证书来源（组件内嵌入的 PEM）。

## Impact

- 组件代码：`common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`（客户端配置）、`common/mqtt_ot_bridge/CMakeLists.txt`（嵌入证书）。
- 新增文件：`common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`。
- 示例配置：`basic_thread_border_router/sdkconfig.defaults`。
- 文档：`common/mqtt_ot_bridge/README.md`、`common/mqtt_ot_bridge/Kconfig.projbuild` 帮助文本。
- 依赖：不新增组件依赖（`espressif__mqtt` 已具备 TLS 能力，底层 mbedTLS 由 IDF 提供）。
- 运行影响：固件体积因嵌入 CA PEM 略增；TLS 握手对堆内存有额外占用。CA 证书有效期至 2038-01-15（DigiCert Global Root G2），远超业务方标注的 2031.11.10。
