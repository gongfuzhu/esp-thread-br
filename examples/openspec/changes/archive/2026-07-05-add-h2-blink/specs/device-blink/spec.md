## ADDED Requirements

### Requirement: blink 命令执行
设备 SHALL 支持 `cmd:"blink"` 命令:收到后立即点亮板载 LED,并在配置的固定时长(`IOT_DEVICE_BLINK_MS`,默认 500ms)后自动熄灭,无需第二条命令。熄灭 MUST 通过 FreeRTOS 单次定时器承载,MUST NOT 在 CoAP 回调中 `vTaskDelay` 阻塞 OpenThread 任务。

#### Scenario: 收到 blink 命令
- **WHEN** 设备的 `/ctrl` 资源收到 `{"reqid":"..","cmd":"blink"}`
- **THEN** 设备立即将 LED 置亮,并启动一个 `IOT_DEVICE_BLINK_MS` 后触发的单次定时器

#### Scenario: 定时熄灭
- **WHEN** blink 定时器到期
- **THEN** 设备将 LED 置灭(在定时器任务上下文,自行持 OT 锁)

#### Scenario: blink 期间再次收到 blink
- **WHEN** LED 处于 blink 亮态期间又收到一条 `blink`
- **THEN** 设备重置定时器周期(延长亮态),不崩溃、不泄漏定时器

### Requirement: blink 执行后上报
设备 SHALL 在 blink 命令执行后,复用现有上行链路单播 NON POST 到 BR 的 `/ack` 资源上报,payload 附带原 `reqid`。上报 MUST 固定 `state:"off"` 并附 `action:"blink"` 标记,以反映 blink 的最终态并标识动作类型,避免依赖竞态读取 GPIO 瞬时电平。

#### Scenario: 单播 blink 上报
- **WHEN** blink 命令经单播下发并被执行
- **THEN** 设备上报 `{"id":<eui64>,"reqid":<reqid>,"state":"off","action":"blink"}`

#### Scenario: 组播 blink 上报加抖动
- **WHEN** blink 命令经组播(本地收包地址为多播)下发并被执行
- **THEN** 设备在 0~500ms 随机抖动后再上报,避免多设备响应风暴

### Requirement: 现有命令契约保持
设备 SHALL 保持 `on`/`off`/`query` 命令的既有语义不变:`on` 常亮、`off` 常灭、`query` 仅上报当前状态。新增 blink MUST NOT 改变这些命令的解析或行为。

#### Scenario: on/off 仍为持续状态
- **WHEN** 设备收到 `cmd:"on"` 或 `cmd:"off"`
- **THEN** LED 进入并保持对应状态,不受 blink 定时器影响
