## Context

`ot_iot_device` 现有命令处理是**单一时间轴**:CoAP `/ctrl` 回调解析命令 → 立即 `device_switch_set()` → 用一个单次定时器 `s_report_timer` 承载"抖动后上报"。约束(硬件联调经验):**绝不能在 CoAP 回调里 `vTaskDelay`**,否则阻塞 OpenThread 任务。

blink 语义("亮 → 固定时长 → 自动灭")引入了**第二个时间轴**:除了"上报"这个延时动作,还需要一个"熄灭"的延时动作。二者独立,不能复用同一个定时器。

## Goals / Non-Goals

**Goals:**
- 新增 `cmd:"blink"`:立即亮、`IOT_DEVICE_BLINK_MS` 后自动灭。
- 熄灭走定时器,不阻塞 OT 任务。
- 复用现有上报链路(单播 NON → BR `/ack`)与组播抖动逻辑。
- 完全非破坏:`on`/`off`/`query` 与 BR 无状态转发层不变。

**Non-Goals:**
- 不支持每命令自定义时长(先固定 Kconfig;后续如需再扩 `duration_ms`)。
- 不支持多次连闪/呼吸灯等复杂图案。
- 不改 BR 侧任何代码或 MQTT topic 结构。

## Decisions

**决策 1:新增独立的单次定时器 `s_blink_timer`,而非复用 `s_report_timer`。**
- 两个延时动作(熄灭 vs 上报)时长不同、语义不同,且可能重叠(上报抖动 0~500ms 与 blink 500ms 会撞)。共用一个定时器会互相覆盖。
- 备选:用一个定时器排两次任务 → 状态机复杂,易错。否决。

**决策 2:上报固定 `state:"off"` + `action:"blink"`,不读 GPIO 瞬时值。**
- report 定时器(抖动)与 blink-off 定时器可能并发,`device_switch_get()` 读到的电平是竞态的。
- blink 的最终态本就是 off,固定上报 off 语义正确;`action:"blink"` 让服务端区分"闪过了"与"被关了"。
- 需要在 `device_report()` 里支持带 action 字段——通过传参而非全局状态,避免竞态。

**决策 3:blink 时长用 Kconfig `IOT_DEVICE_BLINK_MS`(默认 500)。**
- 用户要"固定短闪",但 Kconfig 零成本且比硬编码可调。

**决策 4:blink 命令的上报仍走现有抖动路径。**
- blink 可经组播下发,多设备同时上报会风暴——沿用 `via_multicast` 抖动判断。
- 即 `ctrl_request_handler` 里 blink 分支:先启动 blink-off 定时器点亮,再照常启动 report 定时器上报。

## Risks / Trade-offs

- **[report 与 blink-off 定时器并发读态竞态]** → 决策 2 已消除:不读 GPIO,固定上报 off。
- **[blink 期间再来 blink]** → `xTimerChangePeriod` + `xTimerStart` 重置周期,效果是延长亮态,无害;单槽 `s_blink_timer` 不泄漏。
- **[blink 期间来 off]** → `device_switch_set(false)` 立即灭,blink-off 定时器到点再灭一次(幂等,无害)。可接受,不特殊处理。
- **[定时器任务栈]** → blink-off 回调只调 `device_switch_set`(不放大数组、不 cJSON),栈占用极小,无栈溢出风险(区别于历史上 `publish_registry` 的坑)。

## Open Questions

无。设计要素明确,可直接进入 tasks。
