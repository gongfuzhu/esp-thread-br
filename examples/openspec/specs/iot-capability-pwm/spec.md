# iot-capability-pwm Specification

## Purpose
TBD - created by archiving change add-iot-device-components. Update Purpose after archive.
## Requirements
### Requirement: pwm_set 事件处理
`iot_cap_pwm_set` 组件 SHALL 提供 `iot_cap_pwm_set_init()`，在其中向内核注册 `pwm_set` event 的 handler。handler SHALL 解析 `data.gpio`、`data.freq`（Hz）、`data.duty`（0~100 百分比），经 ESP-IDF LEDC 驱动配置 PWM 输出，并在 `resp_data` 回填 `{"gpio","freq","duty"}`。

#### Scenario: 配置 PWM 输出
- **WHEN** handler 收到 `data:{"gpio":5,"freq":1000,"duty":60}`
- **THEN** GPIO5 输出 1000Hz、占空比 60% 的 PWM，返回 code 0，`resp_data` 回显 `{"gpio":5,"freq":1000,"duty":60}`

#### Scenario: 参数非法
- **WHEN** `duty` 超出 0~100 或 `freq` 非法
- **THEN** handler 返回 code -1，不改变 PWM 输出

### Requirement: LEDC 通道分配状态
组件 SHALL 在内部维护 GPIO→LEDC 通道的分配映射（组件级 static 状态），同一 GPIO 复用同一通道、不同 GPIO 分配不同通道。通道耗尽时 handler SHALL 返回 code -4。

#### Scenario: 同一 GPIO 复用通道
- **WHEN** 对同一 `gpio` 先后两次 `pwm_set`
- **THEN** 复用同一 LEDC 通道，仅更新 freq/duty，不占用新通道

#### Scenario: 通道耗尽
- **WHEN** 请求的 `gpio` 需要新通道但 LEDC 通道已全部占用
- **THEN** handler 返回 code -4（硬件异常）

