# mqtt-bridge Specification

## Purpose

定义 Thread Border Router 上 MQTT 桥接层的行为：作为局域网内的 MQTT 客户端接入 broker，将下行 topic 上的命令按单播/组播路由到 Thread 侧的 CoAP 目标，并将设备侧回来的 CoAP payload 原样发布到上行 topic。桥接层只做字节透传，不解析 payload 语义，不做对账。
## Requirements
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

### Requirement: 下行 topic 订阅与路由
BR SHALL 订阅单播与组播下行 topic,并根据 topic 决定 CoAP 发送方式。topic 前缀 SHALL 可经 Kconfig 配置。

#### Scenario: 收到单播命令
- **WHEN** BR 收到 `cmd/unicast/<eui64>` 消息
- **THEN** BR 以 `<eui64>` 为目标发起单播 CoAP CON 请求,payload 原样透传

#### Scenario: 收到组播命令
- **WHEN** BR 收到 `cmd/multicast` 消息
- **THEN** BR 向 `ff03::1` 发起 CoAP NON 请求,payload 原样透传

### Requirement: 上行响应发布
BR SHALL 通过两个独立的 CoAP 资源接收 Thread 设备的上行 payload,并按资源分流到不同的上行 topic,均原样透传、不解析业务语义、不改写、不对账:

- `ack` 资源(URI 经 Kconfig 配置,默认 `ack`)收到的 payload 发布到 `cmd/resp`,用于**对下行命令的应答**(如 `switch_resp`)以及单播 CON 事务回调的应答。
- `devup` 资源(URI 经 Kconfig 配置,默认 `devup`)收到的 payload 发布到 `dev/up`,用于设备**主动上报**(无对应下行命令,如 motion/boot/heartbeat)。

分流依据为 CoAP URI(接收资源),BR MUST NOT 解析 payload 内容来决定上行 topic。

#### Scenario: 透传命令应答
- **WHEN** BR 通过 CON 事务回调或 `ack` 资源收到设备的应答类 CoAP payload
- **THEN** BR 将该 payload 原样发布到 `cmd/resp`

#### Scenario: 透传主动上报
- **WHEN** BR 通过 `devup` 资源收到设备的主动上报 CoAP payload
- **THEN** BR 将该 payload 原样发布到 `dev/up`

#### Scenario: 按资源分流不看内容
- **WHEN** 两条上行 payload 内容相似但分别到达 `ack` 与 `devup` 资源
- **THEN** BR 仅按接收资源决定上行 topic(`cmd/resp` vs `dev/up`),不解析 payload

### Requirement: Payload 透传
BR SHALL NOT 解析或理解 `cmd/unicast/<eui64>`、`cmd/multicast` 下行与 `cmd/resp`、`dev/up` 上行 payload 的语义(如 on/off、亮度、reqid、event);payload 在 MQTT 与 CoAP 之间双向原样传递。唯一例外为专用 topic `cmd/registry` 上的 `registry_list` 指令(见"registry_list 指令应答"要求):BR 仅解析该专用 topic 的 payload,`cmd/unicast/+`、`cmd/multicast` 下行与 `ack`、`devup` 上行一律字节透传,不做任何解析。

#### Scenario: 不理解语义
- **WHEN** 任意格式的 payload 经过 `cmd/unicast/<eui64>`、`cmd/multicast` 或 `ack`/`devup` 上行资源
- **THEN** BR 不因 payload 内容做任何分支决策,仅按 topic/CoAP 资源路由字节流

### Requirement: registry_list 指令应答
BR SHALL 订阅专用 topic `cmd/registry`。收到该 topic 的 `registry_list` 指令时，BR 自行查询本地 SRP server 表，构造 `{"reqid","eui64","event":"registry_list_resp","code":0,"msg":"success","data":{"list":[{"eui64","ipv6","service"}...]}}` 并发布到 `cmd/resp`，不向 Thread 侧转发。`reqid` SHALL 与下行一致。这是 BR 唯一自解析/自应答的指令，因为只有 BR 能观察 SRP 表；控制类下行 topic（`cmd/unicast/+`、`cmd/multicast`）不受影响，仍保持字节透传。

#### Scenario: 应答 registry_list
- **WHEN** BR 在 `cmd/registry` 收到 `{"reqid":"batch-0001","event":"registry_list","data":{}}`
- **THEN** BR 查 SRP 表，将每个已注册设备的 `{eui64,ipv6,service}` 汇总为 `list` 发布到 `cmd/resp`，`reqid` 与下行一致，不转发到 CoAP

#### Scenario: 控制类下行不受影响
- **WHEN** BR 在 `cmd/unicast/<eui64>` 或 `cmd/multicast` 收到任意指令
- **THEN** BR 不解析 payload，按原有单播/组播路由字节透传到 CoAP

