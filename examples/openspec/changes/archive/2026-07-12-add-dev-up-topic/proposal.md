## Why

设备的**主动事件上报**(motion/boot/heartbeat)和对下行命令的**命令应答**(`switch_resp` 等)当前都通过同一个 CoAP URI(`ack`)进 BR,BR 又把两者不加区分地发到同一个 MQTT topic `otbr/cmd/resp`。后端无法从 topic 区分"这是主动上报还是命令回执",只能靠嗅探 payload 里的 `event` 字段。同时 motion 报文用的是旧字段名 `id`,与 IoT 组件的 `eui64` 信封不一致,后端要维护两套解析。

## What Changes

- 在 BR(`mqtt_ot_bridge`)新增第二个 CoAP 资源 `devup`,其上行发布到新 MQTT topic `otbr/dev/up`;命令应答仍走 `ack` → `otbr/cmd/resp`。BR 保持纯管道,按 **CoAP URI 分流**,不解析 payload。
- `deep_sleep` 示例的 motion/boot/heartbeat 上报改指向 `devup` URI,并将报文统一为新信封 `{reqid, eui64, event, data}`(字段 `id` → `eui64`,补空 `data`),与 IoT 组件同构。
- **BREAKING**:CoAP URI 与 BR 资源需**配对升级**——旧 `deep_sleep` 固件(发 `ack`)配新 BR 会让 motion 掉进 `cmd/resp`;新 `deep_sleep` 配旧 BR 收不到(无 `devup` 资源)。非热兼容。

## Capabilities

### New Capabilities
<!-- none -->

### Modified Capabilities
- `mqtt-bridge`: 上行由单一 `ack`→`cmd/resp` 通道,拆分为两个 CoAP 资源(`ack`→`cmd/resp` 命令应答、`devup`→`dev/up` 主动上报);新增 `devup` URI 配置项。
- `motion-event-report`: 上报报文由 `{id,reqid,event}` 改为统一信封 `{reqid,eui64,event,data}`;上报目标 URI 由 `ack` 改为 `devup`。

## Impact

- 代码:`common/mqtt_ot_bridge/src/mqtt_ot_bridge.c`(参数化上行 + 第二资源)、`common/mqtt_ot_bridge/Kconfig.projbuild`(新增 `MQTT_OT_BRIDGE_DEVUP_URI`)、`deep_sleep/main/esp_ot_sleepy_device.c`(信封字段)、`deep_sleep/main/Kconfig.projbuild`(`MOTION_ACK_URI` 默认改 `devup`)。
- 文档:`common/mqtt_ot_bridge/docs/MQTT_API.md`、`README.md`(新增 `dev/up` 通道与信封说明)。
- 依赖:无新增。BR 与 sleepy device 均已有 OpenThread CoAP。
- 部署:BR 固件与 deep_sleep 固件需同版本配对刷写。
