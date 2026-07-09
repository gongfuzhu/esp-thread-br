## Why

`deep_sleep`(ESP32-H2 Thread Sleepy End Device)当前只演示两种唤醒:20 秒 RTC 定时器和 GPIO 按钮,唤醒后除打印唤醒原因外**不做任何上行**。真实的低功耗传感场景需要:设备平时深睡,**外部运动传感器检测到人时给出 3.3V 高电平唤醒 H2,H2 入网后把"运动事件"送出去**。H2 只有 802.15.4、没有 IP/WiFi,自己发不了 MQTT——事件必须经 Thread mesh 上报到 BR,由既有的 `mqtt_ot_bridge` 透传到 MQTT `dev/response`。

## What Changes

- `deep_sleep` 新增**运动唤醒源**:EXT1 监听 GPIO8(H2 EXT1 可用 RTC GPIO 8–14,避开作 BOOT 的 GPIO9),`ESP_EXT1_WAKEUP_ANY_HIGH`(检测=高电平)。
- 保留 RTC 定时唤醒作为**心跳**(间隔由 20s 提升到分钟级,新增 Kconfig 配置)。
- 唤醒后**移植** `ot_iot_device` 的 SRP 注册 + CoAP 上报路径:入网 → SRP 注册 → 单播 NON POST 到 BR `/ack`,payload 带自生成 `reqid` 与 `event` 字段(`motion` / `heartbeat` / `boot`)。
- **睡眠触发点从"到 CHILD 后固定 5 秒"改为"SRP 注册完成且报文发出之后"**,并加最大清醒时间兜底,避免入网失败时一直清醒耗电。
- 上报 JSON **手写(snprintf)**,不引入 cJSON 托管组件,保持本示例零托管依赖。
- **非破坏性**:BR / `mqtt_ot_bridge` 不改动——`/ack` 处理器原样透传 payload,新增 `event` 字段自动流到 MQTT。

## Capabilities

### New Capabilities
- `motion-event-report`:H2 深睡设备的运动/心跳唤醒 → 入网 → SRP 注册 → 上报事件到 BR `/ack` 的端到端行为。

### Modified Capabilities
<!-- BR 侧 mqtt-bridge / coap-device-control 无需改动:上报走既有 /ack 透传路径,payload 仅新增 event 字段。 -->

## Impact

- **代码**:`deep_sleep/main/esp_ot_sleepy_device.c`(唤醒源配置、SRP 注册、上报、睡眠触发改造)、`deep_sleep/main/Kconfig.projbuild`(**新建**:GPIO、心跳间隔、SRP service、/ack URI、CoAP 端口、最大清醒兜底毫秒)、`deep_sleep/main/CMakeLists.txt`(EUI64 辅助源 + INCLUDE)。可选:从 `ot_iot_device` 复用 `device_eui64.c/.h`。
- **契约**:设备主动上报 `{"id":<eui64>,"reqid":<自生成>,"event":"motion|heartbeat|boot"}` 到 BR `/ack`;`ACK_URI`、CoAP 端口、SRP service 必须与 BR 的 `mqtt_ot_bridge` 配置一致。
- **硬件**:运动传感器 VCC + GND(共地)+ OUT→GPIO8;OUT 高电平须为 3.3V、空闲为低电平(推挽输出无需外部电阻)。
- **依赖**:无新增托管组件;上报用手写 JSON。
