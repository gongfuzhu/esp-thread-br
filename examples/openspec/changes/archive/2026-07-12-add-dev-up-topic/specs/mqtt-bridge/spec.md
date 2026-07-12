## MODIFIED Requirements

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
