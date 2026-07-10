## MODIFIED Requirements

### Requirement: MQTT 客户端连接与登陆
BR SHALL 作为 MQTT 客户端连接到配置的 broker，使用用户名/密码认证。连接 SHALL 支持 TLS：当 broker URI 以 `mqtts://` 指定时，BR SHALL 使用嵌入固件的 CA 证书校验服务器证书；URI 以 `mqtt://` 指定时保持明文连接。连接参数（URI、用户名、密码）SHALL 来自 Kconfig，CA 证书 SHALL 由 `mqtt_ot_bridge` 组件内嵌入的 PEM 提供。

#### Scenario: 成功建立 TLS 连接并认证
- **WHEN** BR 启动，broker URI 为 `mqtts://`，服务器证书由嵌入的 CA 签发且凭据正确
- **THEN** BR 完成 TLS 握手与服务器证书校验，建立连接并订阅所有下行 topic

#### Scenario: 服务器证书校验失败
- **WHEN** broker 出示的证书无法由嵌入的 CA 验证（如证书不匹配或过期）
- **THEN** BR 不建立连接，记录 TLS 错误日志并按 esp-mqtt 默认策略重连，不影响 Thread/BR 网络功能

#### Scenario: 明文连接（向后兼容）
- **WHEN** broker URI 为 `mqtt://`
- **THEN** BR 以明文方式连接，不进行 TLS 握手

#### Scenario: 认证失败
- **WHEN** broker 拒绝提供的用户名/密码
- **THEN** BR 记录错误日志并按 esp-mqtt 默认策略重连，不影响 Thread/BR 网络功能

#### Scenario: broker 暂时不可达
- **WHEN** broker 断线或不可达
- **THEN** BR 自动重连，期间 Thread 网络与 BR 路由功能不受影响
