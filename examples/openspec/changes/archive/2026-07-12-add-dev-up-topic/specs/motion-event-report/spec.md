## MODIFIED Requirements

### Requirement: 唤醒后 SRP 注册并上报事件
设备 SHALL 在唤醒入网后,重新执行 SRP 注册(以 16 位小写十六进制 EUI64 为 host 名),待取得 BR(SRP server)地址后,单播 CoAP NON POST 到 BR 的 `devup` 资源(URI、端口经 Kconfig 配置,须与 BR `mqtt_ot_bridge` 的 `MQTT_OT_BRIDGE_DEVUP_URI` 一致)上报事件。payload MUST 为手写 JSON 信封 `{"reqid":<自生成>,"eui64":<eui64>,"event":<事件类型>,"data":{}}`,字段名 `eui64`(非旧的 `id`),`data` 为空对象占位,MUST NOT 依赖 cJSON 托管组件。

#### Scenario: 运动事件上报
- **WHEN** 因运动唤醒且 SRP 注册取得 server 地址
- **THEN** 设备上报 `{"reqid":<reqid>,"eui64":<eui64>,"event":"motion","data":{}}` 到 BR `devup` 资源

#### Scenario: 心跳事件上报
- **WHEN** 因定时唤醒且 SRP 注册取得 server 地址
- **THEN** 设备上报 `event:"heartbeat"`(同信封格式)

#### Scenario: reqid 自生成
- **WHEN** 构造上报 payload
- **THEN** `reqid` 由设备自行生成(非服务端下发),每次上报唯一

## ADDED Requirements

### Requirement: 上行报文经 dev/up 通道透传
本能力上报走 BR 的 `devup` 资源,BR 原样透传 payload 到 MQTT `<prefix>/dev/up`(主动上报通道),与命令应答通道 `cmd/resp` 分离。设备与 BR 的上报 URI MUST 配对配置(设备 `MOTION_ACK_URI` 与 BR `MQTT_OT_BRIDGE_DEVUP_URI` 默认均为 `devup`)。

#### Scenario: 事件报文透传到 dev/up
- **WHEN** 设备上报统一信封 payload 到 BR `devup` 资源
- **THEN** MQTT `<prefix>/dev/up` 收到原样 payload(含 `eui64`/`event`/`data`),不出现在 `cmd/resp`

#### Scenario: URI 配对
- **WHEN** 设备的上报 URI 与 BR 的 `devup` 资源 URI 一致
- **THEN** 上报被 BR 的 `devup` 资源接收并转发到 `dev/up`

## REMOVED Requirements

### Requirement: 不改动 BR 上行透传
**Reason**: 本 change 引入 `dev/up` 专用上报通道,BR `mqtt_ot_bridge` 必须新增 `devup` CoAP 资源,原"不改动 BR"的约束已不再成立。
**Migration**: 见修改后的"上行报文经 dev/up 通道透传"要求及 `mqtt-bridge` spec 的"上行响应发布"要求;BR 与 sleepy device 固件需配对升级。
