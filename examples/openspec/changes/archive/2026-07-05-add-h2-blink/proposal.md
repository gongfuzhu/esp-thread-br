## Why

当前 `ot_iot_device`(ESP32-H2)只支持持续状态的 `on`/`off`/`query`:`on` 常亮、`off` 常灭。缺少"收到消息后点亮、随即自动熄灭"的瞬时脉冲(blink)行为——这是最直观的"设备收到指令"可视反馈,常用于寻址确认、告警提示或联调时的活体检测。

## What Changes

- H2 设备端新增 `cmd:"blink"` 命令:收到后立即点亮板载 LED,固定时长后自动熄灭,无需第二条命令。
- 亮持续时长由新增 Kconfig 项 `IOT_DEVICE_BLINK_MS`(默认 500ms)配置。
- blink 结束后按现有链路上报状态到 BR `/ack`;上报固定 `state:"off"` 并附 `action:"blink"` 标记,避免依赖竞态读取 GPIO 瞬时值。
- 复用现有组播抖动上报逻辑:blink 命令若经组播下发,上报同样加 0~500ms 抖动防风暴。
- **非破坏性**:`on`/`off`/`query` 契约不变;BR 无状态转发层不改动(payload 原样透传)。

## Capabilities

### New Capabilities
- `device-blink`: H2 设备端对 `cmd:"blink"` 的解析与执行——瞬时点亮、定时自动熄灭、执行后上报。

### Modified Capabilities
<!-- BR 侧 coap-device-control 无需改动:blink 是设备端新命令,BR 仍原样透传 payload。 -->

## Impact

- **代码**:`ot_iot_device/main/iot_device.c`(命令解析 + blink 定时器 + 上报)、`ot_iot_device/main/Kconfig.projbuild`(新增 `IOT_DEVICE_BLINK_MS`)。`device_switch.c/.h` 无需改动(`set(true)/set(false)` 已足够)。
- **契约**:服务端可下发 `{"reqid":"..","cmd":"blink"}`;BR、MQTT topic 结构、`on/off/query` 均不变。
- **依赖**:无新增组件;仅新增一个 FreeRTOS 单次定时器(`s_blink_timer`)。
