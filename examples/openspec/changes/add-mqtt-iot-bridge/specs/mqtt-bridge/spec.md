## ADDED Requirements

### Requirement: MQTT 客户端连接与登陆
BR SHALL 作为 MQTT 客户端连接到局域网内配置的 broker,使用用户名/密码认证,不使用 TLS。连接参数(地址、端口、用户名、密码)SHALL 来自 Kconfig。

#### Scenario: 成功连接并认证
- **WHEN** BR 启动且 broker 可达、凭据正确
- **THEN** MQTT 客户端建立连接并订阅所有下行 topic

#### Scenario: 认证失败
- **WHEN** broker 拒绝提供的用户名/密码
- **THEN** BR 记录错误日志并按 esp-mqtt 默认策略重连,不影响 Thread/BR 网络功能

#### Scenario: broker 暂时不可达
- **WHEN** broker 断线或不可达
- **THEN** BR 自动重连,期间 Thread 网络与 BR 路由功能不受影响

### Requirement: 下行 topic 订阅与路由
BR SHALL 订阅单播与组播下行 topic,并根据 topic 决定 CoAP 发送方式。topic 前缀 SHALL 可经 Kconfig 配置。

#### Scenario: 收到单播命令
- **WHEN** BR 收到 `cmd/unicast/<eui64>` 消息
- **THEN** BR 以 `<eui64>` 为目标发起单播 CoAP CON 请求,payload 原样透传

#### Scenario: 收到组播命令
- **WHEN** BR 收到 `cmd/multicast` 消息
- **THEN** BR 向 `ff03::1` 发起 CoAP NON 请求,payload 原样透传

### Requirement: 上行响应发布
BR SHALL 把从 Thread 设备收到的每条 CoAP 响应/上报的 payload 原样发布到上行 topic,不解析、不改写、不对账。

#### Scenario: 透传设备响应
- **WHEN** BR 通过 CON 事务回调或 `/ack` 资源收到设备的 CoAP payload
- **THEN** BR 将该 payload 原样发布到 `dev/response`

### Requirement: Payload 透传
BR SHALL NOT 解析或理解下行/上行 payload 的语义(如 on/off、亮度、reqid);payload 在 MQTT 与 CoAP 之间双向原样传递。

#### Scenario: 不理解语义
- **WHEN** 任意格式的 payload 经过 BR
- **THEN** BR 不因 payload 内容做任何分支决策,仅按 topic/CoAP 目标路由字节流
