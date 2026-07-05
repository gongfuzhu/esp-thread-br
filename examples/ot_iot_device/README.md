# ot_iot_device

ESP32-H2 上的示例 IoT 开关设备，作为 `common/mqtt_ot_bridge` 的受控端。
从 `ot_cli` 派生，保留 OpenThread CLI 便于调试，额外提供：SRP 自动注册、
CoAP `ctrl` 资源接收开关命令、执行后单播上报到 BR 的 `ack` 资源。

## 编译烧录（ESP-IDF PowerShell）

    idf.py set-target esp32h2
    idf.py build
    idf.py -p COM<x> flash monitor

> 注意：`idf.py` 必须在 ESP-IDF PowerShell/CMD 运行，不能在 Git Bash（会报
> "MSys/Mingw is no longer supported"）。

## 配置（menuconfig → IoT Device Example）

- `IOT_DEVICE_SWITCH_GPIO`：开关/LED 的 GPIO（H2 devkit 板载 LED 默认 8）
- `IOT_DEVICE_SERVICE_NAME`：SRP 服务名（默认 `_iot._udp`）
- `IOT_DEVICE_CTRL_URI` / `IOT_DEVICE_ACK_URI`：与 BR 的对应 Kconfig 必须一致
- `IOT_DEVICE_MULTICAST_ADDR`：组播组（默认 `ff03::1`，须与 BR 一致）
- `IOT_DEVICE_COAP_PORT`：CoAP 端口（默认 5683）

## 身份与契约

- 设备身份 = 出厂 EUI64（`otPlatRadioGetIeeeEui64`），16 位小写十六进制。
- SRP host name 与 service instance name 均为该 EUI64，BR 靠 host 名前 16 字符匹配。
- MQTT 侧用 `otbr/cmd/unicast/<eui64>` 单播控制、`otbr/cmd/multicast` 组播控制。

## 命令 payload（服务端定义，设备用 cJSON 解析）

| 字段 | 说明 |
|------|------|
| `reqid` | 服务端生成的唯一标识，设备原样回带用于对账 |
| `cmd` | `on` / `off` / `query` |

上报 payload：`{"id":<eui64>,"reqid":<reqid>,"state":"on|off"}`，
经 BR 透传到 `otbr/dev/response`。服务端凭自带的 reqid 自行对账。

## 组播抖动

收到组播命令后，设备不在 CoAP 回调里直接延时（会阻塞 OpenThread 任务），
而是用单次 FreeRTOS 定时器承载 0~500ms 随机抖动后再上报，避免多设备同时
上报造成响应风暴。单播命令抖动为 0（尽快上报）。

## 端到端联调

1. 先让 BR（`basic_thread_border_router`，启用 `mqtt_ot_bridge`）组网并连上 broker。
2. 本设备上电入网 → SRP 注册 → BR 的 `otbr/dev/registry` 出现本设备 EUI64。
3. 向 `otbr/cmd/unicast/<eui64>` 或 `otbr/cmd/multicast` 发命令，观察 LED 与
   `otbr/dev/response`。

### 预期设备日志

```
SRP registration queued: host/instance=<eui64> service=_iot._udp
CoAP started, resource 'ctrl' registered
subscribe multicast ff03::1 err=0
SRP auto-start: server found, host=<eui64>
ctrl: set switch=1 reqid=u1
reported state=on reqid=u1
```
