## ADDED Requirements

### Requirement: switch 事件处理
`iot_cap_switch` 组件 SHALL 提供 `iot_cap_switch_init()`，在其中向内核注册 `switch` event 的 handler。handler SHALL 解析 `data.gpio`（GPIO 编号）与 `data.action`（`"on"`/`"off"`），驱动对应 GPIO 电平，并在 `resp_data` 回填 `{"gpio":<n>,"status":"on"/"off"}`。GPIO 为无状态硬件，直接经 ESP-IDF `driver` 操作，不缓存电平。

#### Scenario: 打开指定 GPIO
- **WHEN** handler 收到 `data:{"gpio":2,"action":"on"}`
- **THEN** GPIO2 置高，返回 code 0，`resp_data` 为 `{"gpio":2,"status":"on"}`

#### Scenario: 关闭指定 GPIO
- **WHEN** handler 收到 `data:{"gpio":2,"action":"off"}`
- **THEN** GPIO2 置低，返回 code 0，`resp_data` 为 `{"gpio":2,"status":"off"}`

#### Scenario: 参数缺失或非法
- **WHEN** `data` 缺少 `gpio` 或 `action` 非 `on`/`off`
- **THEN** handler 返回 code -1，不改变任何 GPIO 状态

### Requirement: switch 延时与保持参数（可选）
handler SHALL 接受可选的 `data.delay`（执行前延时毫秒）与 `data.hold`（保持后自动回到相反态的毫秒）。`delay`/`hold` 非零时 MUST 经 FreeRTOS 定时器承载，MUST NOT 在 CoAP 回调中阻塞 OpenThread 任务。省略或为 0 时行为等同立即执行且不自动回复。

#### Scenario: hold 后自动回复
- **WHEN** handler 收到 `data:{"gpio":2,"action":"on","hold":500}`
- **THEN** GPIO2 立即置高，约 500ms 后经定时器自动置低，不阻塞 OT 任务
