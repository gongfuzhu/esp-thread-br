## MODIFIED Requirements

### Requirement: 现有命令契约保持
设备 SHALL 通过 `switch` event 表达开关语义，取代旧的 `{reqid,cmd:"on/off/query"}` 协议：`data.action:"on"` 常亮、`data.action:"off"` 常灭。查询语义由内核统一响应信封回显当前状态承载，不再使用独立 `query` cmd。

#### Scenario: on/off 仍为持续状态
- **WHEN** 设备收到 `{"reqid":"..","event":"switch","data":{"gpio":<led>,"action":"on"}}` 或 `action:"off"`
- **THEN** LED 进入并保持对应状态

### Requirement: blink 命令执行
设备 SHALL 通过 `switch` event 的可选 `hold` 参数表达 blink 语义：`{"event":"switch","data":{"gpio":<led>,"action":"on","hold":<ms>}}` 收到后立即点亮 LED，并在 `hold` 毫秒后自动熄灭，无需第二条命令。熄灭 MUST 通过 FreeRTOS 单次定时器承载，MUST NOT 在 CoAP 回调中 `vTaskDelay` 阻塞 OpenThread 任务。`hold` 默认值经 Kconfig 配置。

#### Scenario: 收到 hold 命令（blink）
- **WHEN** 设备收到 `{"reqid":"..","event":"switch","data":{"gpio":<led>,"action":"on","hold":500}}`
- **THEN** 设备立即将 LED 置亮，并启动一个 500ms 后触发的单次定时器

#### Scenario: 定时熄灭
- **WHEN** hold 定时器到期
- **THEN** 设备将 LED 置灭(在定时器任务上下文,自行持 OT 锁)

#### Scenario: hold 期间再次收到 hold
- **WHEN** LED 处于 hold 亮态期间又收到一条带 `hold` 的 switch
- **THEN** 设备重置定时器周期(延长亮态),不崩溃、不泄漏定时器

### Requirement: blink 执行后上报
设备 SHALL 在命令执行后由内核统一封装响应信封上报到 BR 的 `/ack` 资源，`reqid` 与下行一致，根节点带 `eui64`。响应 `data` MUST 反映执行结果（如 `{"gpio","status"}`）。组播下发时上报 MUST 加 0~500ms 随机抖动，避免多设备响应风暴。

#### Scenario: 单播上报
- **WHEN** 命令经单播下发并被执行
- **THEN** 设备上报 `{"reqid":"..","eui64":"<本机>","event":"switch_resp","code":0,"msg":"success","data":{"gpio":<n>,"status":".."}}`

#### Scenario: 组播上报加抖动
- **WHEN** 命令经组播(本地收包地址为多播)下发并被执行
- **THEN** 设备在 0~500ms 随机抖动后再上报
