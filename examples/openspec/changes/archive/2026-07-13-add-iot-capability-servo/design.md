## Context

舵机（如 SG90/MG90）由 50Hz PWM 的脉宽定位角度：脉宽在一个 20ms 周期内决定舵盘转角。现有 `iot_cap_pwm_set` 暴露的是通用 freq/duty%，语义不匹配 §8.1 的 `{gpio,angle}`，且分辨率与 timer 共享两处硬伤（见 proposal）。本组件为 `servo_set` 提供专用、干净的实现。

## Goals / Non-Goals

- Goals: 落地协议 §8.1 `servo_set`；angle→脉宽换算封在固件；与 `pwm_set` timer 隔离。
- Non-Goals: 舵机速度/缓动控制、多圈舵机、连续旋转舵机（本次仅标准角度舵机）；周期上报。

## Decisions

### 脉宽映射：0.5–2.5ms 固定
0°=0.5ms、90°=1.5ms、180°=2.5ms 线性映射，写死在组件内，报文只传 `{gpio,angle}`，严格对齐 §8.1 契约。

- 50Hz → 周期 20ms。用 LEDC 定时器分辨率（拟用 `LEDC_TIMER_13_BIT`，8192 级）表达占空比：
  - 占空比 raw = round(pulse_ms / 20ms * (2^13 - 1))
  - 0.5ms → ~205、1.5ms → ~614、2.5ms → ~1024
- 角度分辨率：(2.5-0.5)ms / 180° ≈ 11.1µs/°，LEDC 13-bit 下每步约 2.44µs，足以区分每一度，远优于 pwm_set 整数 duty% 的 ~1.8°/步。

*为何不做可配 min/max 脉宽*：会污染 §8.1 的 `{gpio,angle}` 报文契约、增加 handler 复杂度；标准 0.5–2.5ms 覆盖绝大多数舵机，需要时再迭代。

### 独占 LEDC timer
组件使用 `LEDC_TIMER_1`（`iot_cap_pwm_set` 用 `LEDC_TIMER_0`），首次调用时 `ledc_timer_config` 锁 50Hz，之后不再改 freq，只更新占空比。避免 pwm_set 改频时把舵机 50Hz 覆盖掉导致失步。

- 分辨率选 13-bit 是因为 50Hz 下 LEDC 需较高分辨率位数才能匹配时钟；沿用 pwm_set 的 `LEDC_LOW_SPEED_MODE`。

### 通道分配
复用 pwm_set 同样的 static 映射思路（GPIO→channel，同 GPIO 复用、耗尽回 -4），但为组件私有状态、独立于 pwm_set 的通道表。注：LEDC 通道是全局资源，servo 从高端(LEDC_CHANNEL_MAX-1 向下)分配，pwm_set 从低端(0 向上)分配，二者合计不得超过 `LEDC_CHANNEL_MAX`；超出时 servo 分配返回 -4。

## Risks / Trade-offs

- LEDC 通道全局共享：servo + pwm + 其它 LEDC 用途合计受 `LEDC_CHANNEL_MAX` 限制。缓解：耗尽明确回 code -4，联调时注意分配。
- 50Hz + 13-bit 在部分目标芯片时钟组合下可能受 LEDC 约束；以编译 + 设备端 monitor 验证实际输出。

## Migration Plan

纯新增能力 + 主程序加一行 `iot_cap_servo_set_init()`，无破坏性变更。此前 `servo_set` 恒回 code:-3，上线后开始正常应答，云端无需改报文格式（§8.1 早已定义）。

## Open Questions

- 无（脉宽区间与 timer 隔离已定）。
