## Why

设备端固件（`ot_iot_device`）当前把 SRP 注册、CoAP 传输、命令分发和 GPIO 驱动全平铺在 `main/` 里，命令协议是简化的 `{reqid,cmd:"on/off/query/blink"}`，无法复用到新项目、也无法按产品裁剪功能。`物联网IoT指令响应协议规范` 定义了一套统一信封（`{reqid,event,data}` / `{reqid,eui64,event_resp,code,msg,data}`）和一片外设指令面（GPIO/PWM/ADC/DAC/舵机/红外）。本次把设备端能力拆成**可复用的公共组件**：一个协议+传输内核，加上一 event 一组件的可插拔能力，项目按需引用即可实现"功能可配置"。周期上报（`sensor_report`/`report_freq`）本次不做，留待加入传感器能力时一并设计。

## What Changes

- 新建 `common/iot_device_core` 内核组件：SRP 自动注册、CoAP server（`ctrl` 资源 + 组播订阅）、统一报文信封解析/封装、`event → handler` 分发表、独立命令 worker 任务（handler 与 OT 任务解耦、可阻塞）、单播应答上报器。
- 新建 3 个样板能力组件（一 event 一组件，显式 `xxx_init()` 注册）：`iot_cap_switch`（GPIO 类）、`iot_cap_pwm_set`（LEDC 类）、`iot_cap_adc_read`（ADC 查询类）。三者各代表一类外设，为后续 `switch_toggle`/`switch_batch`/`gpio_read`/`dac_set`/`servo_set`/`ir_send` 打模板。
- **BREAKING** 改造 `ot_iot_device` 示范工程：弃用 `{reqid,cmd}` 协议，改为 `app_main` 显式挂载内核 + 3 个能力；旧 `on/off/query/blink` 语义映射到新的 `switch` event。
- **BREAKING** 修改 `common/mqtt_ot_bridge`：上行响应主题 `dev/response` → `cmd/resp`；新增专用下行 topic `cmd/registry`，BR 在其上应答 `registry_list` 指令（查 SRP 表回 `registry_list_resp`）；控制类下行 `cmd/unicast/+`、`cmd/multicast` 仍字节透传。

## Capabilities

### New Capabilities
- `iot-command-protocol`: 云端↔设备的统一报文信封契约——下行 `{reqid,event,data}`、上行响应 `{reqid,eui64,event_resp,code,msg,data}`、全局状态码（0/-1/-2/-3/-4）、`_resp` 后缀与 reqid 配对规则。
- `iot-device-core`: 设备端内核框架——SRP 注册、CoAP 传输、命令 worker 任务、event 分发注册表、响应信封封装、单播应答上报器。
- `iot-capability-switch`: `switch` event 能力组件（GPIO 开关，含 `action/delay/hold` 参数）。
- `iot-capability-pwm`: `pwm_set` event 能力组件（LEDC 频率/占空比配置，含通道分配状态）。
- `iot-capability-adc`: `adc_read` event 能力组件（ADC 采样，返回 `raw_val`/`voltage`）。

### Modified Capabilities
- `mqtt-bridge`: 响应上行主题改名为 `cmd/resp`；新增专用 topic `cmd/registry` 由 BR 应答 `registry_list`；控制类下行仍纯透传。
- `device-blink`: 设备端命令协议从 `{reqid,cmd}` 迁移到 `{reqid,event,data}`；`blink`/`on`/`off`/`query` 映射为 `switch` event 语义。

（`coap-device-control` 的 BR CoAP 无状态转发行为不变，本次不产生 spec 变更。）

## Impact

- 新增组件目录：`common/iot_device_core`、`common/iot_cap_switch`、`common/iot_cap_pwm_set`、`common/iot_cap_adc_read`。
- 改造工程：`ot_iot_device/main`（弃用旧协议、显式挂载组件）。
- 修改组件：`common/mqtt_ot_bridge`（响应主题改名、`cmd/registry` 订阅与 `registry_list` 应答逻辑，Kconfig 主题项）。
- 依赖：内核与能力组件依赖 `espressif/cjson`、`openthread`、IDF `driver`（gpio/ledc/adc）。
- 云端/服务端：MQTT 主题与报文格式变更（`cmd/resp`、`cmd/registry`、`event` 分发），需同步联调。
- 无主机端测试套件，验证靠编译 + 设备端 monitor（BR ESP32-C6 + ESP32-H2 设备）。
