## Context

`mqtt_ot_bridge` 组件当前只做明文 MQTT 连接（`mqtt_ot_bridge.c:355` 的 `esp_mqtt_client_config_t` 仅设置 `broker.address.uri` / `credentials`，无任何 TLS 字段）。Kconfig 默认与 README 都假定局域网、无 TLS。现在要接入 EMQX Cloud Serverless（`ibf71a0f.ala.cn-hangzhou.emqxsl.cn:8883`），该 broker 强制 TLS 并要校验服务器证书。

参考现状：`basic_thread_border_router/main/CMakeLists.txt` 已用 `EMBED_TXTFILES` 嵌入 `server_certs/ca_cert.pem`，但那是 **OTA** 用途（`esp_ot_br.c:101` 的 `esp_set_ota_server_cert`），与 MQTT 无关，符号名也不同，不复用。

约束：不改 `sdkconfig`（自动生成）；目标/覆盖项走 `sdkconfig.defaults*`。无主机端测试，验证靠编译 + 设备端 monitor。IDF v6.0.2。

## Goals / Non-Goals

**Goals:**
- 让桥接组件能以 `mqtts://` 连接并用嵌入的 CA PEM 校验服务器证书。
- CA 证书归属组件（`common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`），组件自包含、可移植。
- 保持对 `mqtt://` 明文连接的向后兼容（不设 CA 时 esp-mqtt 按 URI scheme 决定是否 TLS）。
- 更新 `basic_thread_border_router` 的连接参数指向 emqxsl。

**Non-Goals:**
- 不支持 WebSocket（8084）——固件端走原生 8883。
- 不做双向 mTLS（不提供客户端证书）——仅校验服务器 + 用户名/密码认证。
- 不做凭据脱敏/加密存储——`QWERasdf` 按现有 WiFi 密码惯例写入 `sdkconfig.defaults`。
- 不改动 CoAP、SRP、topic 路由等其余桥接逻辑。

## Decisions

**决策 1：CA 证书通过组件 `EMBED_TXTFILES` 嵌入，而非证书 bundle。**
用户明确要求使用其提供的 CA PEM（DigiCert Global Root G2）。在组件 `CMakeLists.txt` 增加 `EMBED_TXTFILES "certs/mqtt_ca_cert.pem"`，源码用 `extern const uint8_t _binary_mqtt_ca_cert_pem_start[]` 引用，赋给 `cfg.broker.verification.certificate`。
- 备选：`esp_crt_bundle_attach`（IDF 自带 Mozilla 根证书 bundle）。DigiCert G2 大概率在 bundle 中，能不嵌证书。但用户显式选择「我提供 CA PEM」，且嵌入指定 CA 更确定、攻击面更小。
- 文件名 `mqtt_ca_cert.pem` 派生出的 asm 符号与 main 的 OTA `ca_cert.pem` 不同名，无符号冲突。

**决策 2：证书指针用 `.pem` 形式传入，配 `certificate_len`（或依赖 NUL 结尾）。**
`EMBED_TXTFILES` 会追加 NUL 终止符，esp-mqtt 接受以 NUL 结尾的 PEM 字符串。设置 `cfg.broker.verification.certificate = (const char *)_binary_..._start`。如需显式长度，用 `_end - _start`。

**决策 3：TLS 与否由 URI scheme 驱动，代码始终挂上 CA。**
始终设置 `verification.certificate`；当 URI 为 `mqtt://` 时 esp-mqtt 不启用 TLS，CA 被忽略——因此明文向后兼容自然成立，无需分支。

**决策 4：连接参数改在 `sdkconfig.defaults`，Kconfig 默认值可一并更新帮助文本。**
`sdkconfig.defaults` 加 `CONFIG_MQTT_OT_BRIDGE_BROKER_URI` / `_USERNAME` / `_PASSWORD` 三行覆盖。Kconfig 的 `default` 保留通用占位，仅更新 help 文本去掉「不使用 TLS」。

## Risks / Trade-offs

- [固件体积 / 堆占用增加] → CA PEM ~1.2KB，可忽略；TLS 握手需额外堆（mbedTLS）。BR 主机（ESP32-C5/P4 等）内存充足，风险低；若 OOM 可在 monitor 观察。
- [CA 过期导致连接失败] → DigiCert Global Root G2 有效期至 2038-01-15，远超业务标注的 2031.11.10；到期前需换证。
- [凭据入库] → `QWERasdf` 明文进 git，与现有 WiFi 密码同等风险。已在 proposal 标注可后续脱敏，本次不处理。
- [无自动化测试] → 只能靠 `idf.py build` + 设备端 monitor 验证 TLS 握手成功与订阅日志。
- [emqxsl 证书链] → 若 broker 实际由中间 CA 签发而客户端只嵌根证书，需确认 mbedTLS 能构建完整链（通常服务器会下发中间证书）。握手失败时优先排查此项。

## Migration Plan

1. 新增 `common/mqtt_ot_bridge/certs/mqtt_ca_cert.pem`。
2. 改组件 `CMakeLists.txt` 与 `mqtt_ot_bridge.c`。
3. 改 `basic_thread_border_router/sdkconfig.defaults` 连接参数。
4. `idf.py build` 后 `idf.py -p <PORT> flash monitor`，观察 MQTT 连接/订阅日志确认 TLS 握手成功。
- 回滚：还原三个文件即可；`mqtt://` 兼容性保证旧配置仍可用。

## Open Questions

- 无阻塞性问题。若首次握手失败，优先核对 emqxsl 是否需要 SNI（esp-mqtt 默认按 host 设置 SNI，通常无需额外配置）及证书链完整性。
