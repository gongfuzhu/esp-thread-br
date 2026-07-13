# iot-capability-servo Specification

## Purpose
TBD - created by archiving change add-iot-capability-servo. Update Purpose after archive.
## Requirements
### Requirement: servo_set 事件处理
`iot_cap_servo_set` 组件 SHALL 提供 `iot_cap_servo_set_init()`，在其中向内核注册 `servo_set` event 的 handler。handler SHALL 解析 `data.gpio` 与 `data.angle`（0~180 整数度），按 0.5–2.5ms 固定脉宽映射（0°→0.5ms、90°→1.5ms、180°→2.5ms）在 50Hz 周期上换算 LEDC 占空比并驱动 PWM 输出，成功时返回 code 0 并在 `resp_data` 回填 `{"gpio","angle"}`。

#### Scenario: 设置舵机角度
- **WHEN** handler 收到 `data:{"gpio":13,"angle":90}`
- **THEN** GPIO13 输出 50Hz、脉宽 1.5ms 的 PWM，返回 code 0，`resp_data` 回显 `{"gpio":13,"angle":90}`

#### Scenario: 角度边界
- **WHEN** handler 收到 `angle:0` 或 `angle:180`
- **THEN** 分别输出 0.5ms、2.5ms 脉宽，返回 code 0

#### Scenario: 参数非法
- **WHEN** `angle` 超出 0~180，或 `gpio`/`angle` 缺失/非数字
- **THEN** handler 返回 code -1，不改变 PWM 输出

### Requirement: 独占 LEDC timer 锁定 50Hz
组件 SHALL 使用一个独立于 `iot_cap_pwm_set` 的 LEDC timer（如 `LEDC_TIMER_1`），固定 50Hz、锁定周期，使舵机 PWM 与 `pwm_set` 的可变频负载互不干扰。组件 SHALL 在内部维护 GPIO→LEDC 通道分配映射：同一 GPIO 复用同一通道，不同 GPIO 分配不同通道，通道耗尽时返回 code -4。

#### Scenario: 同一舵机 GPIO 复用通道
- **WHEN** 对同一 `gpio` 先后两次 `servo_set` 不同角度
- **THEN** 复用同一 LEDC 通道，仅更新占空比，不占用新通道

#### Scenario: 与 pwm_set 共存
- **WHEN** 设备同时挂载 `iot_cap_servo_set` 与 `iot_cap_pwm_set`，且 `pwm_set` 改变其 timer 频率
- **THEN** 舵机 timer 频率保持 50Hz 不受影响

#### Scenario: 通道耗尽
- **WHEN** 请求的 `gpio` 需要新通道但 LEDC 通道已全部占用
- **THEN** handler 返回 code -4（硬件异常）

