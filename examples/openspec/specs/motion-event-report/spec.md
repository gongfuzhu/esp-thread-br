# motion-event-report Specification

## Purpose
TBD - created by archiving change add-motion-sensor. Update Purpose after archive.
## Requirements
### Requirement: 运动传感器 EXT1 唤醒
设备 SHALL 配置 EXT1 深睡唤醒源监听运动传感器输出:唤醒引脚为一个 H2 合法 RTC GPIO(默认 GPIO8,可经 Kconfig 配置),唤醒条件为 `ESP_EXT1_WAKEUP_ANY_HIGH`(检测到运动=引脚高电平)。该唤醒源 MUST 与 RTC 定时唤醒源并存。

#### Scenario: 运动触发唤醒
- **WHEN** 设备处于深睡且运动传感器在唤醒引脚上给出 3.3V 高电平
- **THEN** 设备从深睡唤醒,且 `esp_sleep_get_wakeup_causes()` 含 `BIT(ESP_SLEEP_WAKEUP_EXT1)`

#### Scenario: 唤醒引脚可配置
- **WHEN** 通过 Kconfig 设置唤醒 GPIO
- **THEN** EXT1 配置使用该引脚;默认值为 8

### Requirement: 定时心跳唤醒
设备 SHALL 保留 RTC 定时器唤醒作为周期性"存活"心跳,间隔通过 Kconfig 配置(默认为分钟级,大于原示例的 20 秒)。

#### Scenario: 心跳唤醒
- **WHEN** 深睡时长达到配置的心跳间隔
- **THEN** 设备从深睡唤醒,且唤醒原因含 `BIT(ESP_SLEEP_WAKEUP_TIMER)`

### Requirement: 唤醒原因映射为事件类型
设备 SHALL 在唤醒后根据 `esp_sleep_get_wakeup_causes()` 确定事件类型:EXT1→`"motion"`,TIMER→`"heartbeat"`,UNDEFINED(上电首启)→`"boot"`。该事件类型 MUST 用于后续上报的 `event` 字段。

#### Scenario: 运动事件
- **WHEN** 唤醒原因含 EXT1
- **THEN** 事件类型为 `"motion"`

#### Scenario: 心跳事件
- **WHEN** 唤醒原因含 TIMER(且不含 EXT1)
- **THEN** 事件类型为 `"heartbeat"`

#### Scenario: 首次上电
- **WHEN** 不是深睡复位(唤醒原因为 UNDEFINED)
- **THEN** 事件类型为 `"boot"`

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

### Requirement: 上报完成后再进深睡
设备 SHALL 在上报报文发出后才进入深睡,MUST NOT 沿用"到 CHILD 后固定延时即睡"的旧逻辑(该逻辑会在 SRP 未就绪时丢弃上报)。设备 SHALL 另设一个最大清醒兜底(Kconfig 配置),无论上报成功与否,到点强制进深睡,以防 attach 失败时无限清醒耗电。

#### Scenario: 正常上报后睡眠
- **WHEN** 事件上报报文已成功交给 radio 发送
- **THEN** 设备调用 `esp_deep_sleep_start()` 回到深睡

#### Scenario: 入网失败兜底
- **WHEN** 唤醒后在最大清醒兜底时间内始终未能完成 SRP 注册或上报
- **THEN** 设备到点仍强制进深睡,不持续清醒

### Requirement: 上行报文经 dev/up 通道透传
本能力上报走 BR 的 `devup` 资源,BR 原样透传 payload 到 MQTT `<prefix>/dev/up`(主动上报通道),与命令应答通道 `cmd/resp` 分离。设备与 BR 的上报 URI MUST 配对配置(设备 `MOTION_ACK_URI` 与 BR `MQTT_OT_BRIDGE_DEVUP_URI` 默认均为 `devup`)。

#### Scenario: 事件报文透传到 dev/up
- **WHEN** 设备上报统一信封 payload 到 BR `devup` 资源
- **THEN** MQTT `<prefix>/dev/up` 收到原样 payload(含 `eui64`/`event`/`data`),不出现在 `cmd/resp`

#### Scenario: URI 配对
- **WHEN** 设备的上报 URI 与 BR 的 `devup` 资源 URI 一致
- **THEN** 上报被 BR 的 `devup` 资源接收并转发到 `dev/up`

