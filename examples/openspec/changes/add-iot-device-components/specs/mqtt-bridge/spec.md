## MODIFIED Requirements

### Requirement: 上行响应发布
BR SHALL 把从 Thread 设备收到的每条 CoAP 响应的 payload 原样发布到上行 topic `cmd/resp`，不解析业务语义、不改写、不对账。

#### Scenario: 透传设备应答
- **WHEN** BR 通过 CON 事务回调或 `/ack` 资源收到设备的应答类 CoAP payload
- **THEN** BR 将该 payload 原样发布到 `cmd/resp`

### Requirement: Payload 透传
BR SHALL NOT 解析或理解 `cmd/unicast/<eui64>`、`cmd/multicast` 下行与上行 payload 的语义(如 on/off、亮度、reqid);payload 在 MQTT 与 CoAP 之间双向原样传递。唯一例外为专用 topic `cmd/registry` 上的 `registry_list` 指令（见"registry_list 指令应答"要求）：BR 仅解析该专用 topic 的 payload，`cmd/unicast/+` 与 `cmd/multicast` 一律字节透传，不做任何解析。

#### Scenario: 不理解语义
- **WHEN** 任意格式的 payload 经过 `cmd/unicast/<eui64>` 或 `cmd/multicast`
- **THEN** BR 不因 payload 内容做任何分支决策,仅按 topic/CoAP 目标路由字节流

## ADDED Requirements

### Requirement: registry_list 指令应答
BR SHALL 订阅专用 topic `cmd/registry`。收到该 topic 的 `registry_list` 指令时，BR 自行查询本地 SRP server 表，构造 `{"reqid","eui64","event":"registry_list_resp","code":0,"msg":"success","data":{"list":[{"eui64","ipv6","service"}...]}}` 并发布到 `cmd/resp`，不向 Thread 侧转发。`reqid` SHALL 与下行一致。这是 BR 唯一自解析/自应答的指令，因为只有 BR 能观察 SRP 表；控制类下行 topic（`cmd/unicast/+`、`cmd/multicast`）不受影响，仍保持字节透传。

#### Scenario: 应答 registry_list
- **WHEN** BR 在 `cmd/registry` 收到 `{"reqid":"batch-0001","event":"registry_list","data":{}}`
- **THEN** BR 查 SRP 表，将每个已注册设备的 `{eui64,ipv6,service}` 汇总为 `list` 发布到 `cmd/resp`，`reqid` 与下行一致，不转发到 CoAP

#### Scenario: 控制类下行不受影响
- **WHEN** BR 在 `cmd/unicast/<eui64>` 或 `cmd/multicast` 收到任意指令
- **THEN** BR 不解析 payload，按原有单播/组播路由字节透传到 CoAP
