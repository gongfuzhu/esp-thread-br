# mqtt_ot_bridge

BR 侧的无状态 MQTT↔CoAP 桥接组件。把局域网 MQTT 命令翻译为对 Thread 网内设备的 CoAP 请求,并把设备响应与 SRP 设备清单发布回 MQTT。

> **后端/服务端对接**请看 [`docs/MQTT_API.md`](docs/MQTT_API.md)——含 topic 总表、
> 各 payload 字段与示例、reqid 对账流程。本 README 侧重 BR 固件配置与设备端契约。

## BR 配置(menuconfig → MQTT OT Bridge)
- `MQTT_OT_BRIDGE_ENABLE`：启用桥接
- `MQTT_OT_BRIDGE_BROKER_URI`：`mqtt://host:port`(明文) 或 `mqtts://host:port`(TLS，用组件内嵌 CA 校验)
- `MQTT_OT_BRIDGE_USERNAME` / `_PASSWORD`：登陆凭据
- `MQTT_OT_BRIDGE_TOPIC_PREFIX`：topic 前缀(默认 `otbr`)
- `MQTT_OT_BRIDGE_MULTICAST_ADDR`：组播地址(默认 `ff03::1`)
- `MQTT_OT_BRIDGE_COAP_URI`：设备下行资源(默认 `ctrl`)
- `MQTT_OT_BRIDGE_ACK_URI`：BR 上行资源(默认 `ack`)
- `MQTT_OT_BRIDGE_COAP_PORT`：CoAP 端口(默认 5683)
- `MQTT_OT_BRIDGE_REGISTRY_INTERVAL_S`：清单发布周期

**前置：** SRP server 默认已启用。IDF v6.0.2 中它是 OpenThread 编译期特性
(`OPENTHREAD_CONFIG_SRP_SERVER_ENABLE=1`，见 `openthread-core-esp32x-ftd-config.h`)，
ESP32 FTD/BR 固件默认开启，**无需** 也**不应**在 sdkconfig 中添加 `CONFIG_OPENTHREAD_SRP_SERVER`
(该 Kconfig 符号不存在)。

## MQTT Topic 约定
| 方向 | Topic | 说明 |
|------|-------|------|
| 下行单播 | `<prefix>/cmd/unicast/<eui64>` | payload 透传进 CoAP CON |
| 下行组播 | `<prefix>/cmd/multicast` | payload 透传进 CoAP NON → 组播地址 |
| 上行响应 | `<prefix>/cmd/resp` | 设备对下行命令的应答 payload 原样发布 |
| 主动上报 | `<prefix>/dev/up` | 设备经 BR `devup` 资源上报的事件,信封 `{reqid,eui64,event,data}` |
| 设备清单 | `<prefix>/dev/registry` | retained，`[{eui64,ipv6,service}]` |

> **配对升级**:`devup` 资源与设备 `MOTION_ACK_URI` 需同版本刷写。旧设备(发 `ack`)
> 配新 BR 会让主动上报误落 `cmd/resp`;新设备配旧 BR(无 `devup` 资源)则收不到。

BR **不解析** payload：reqid/语义由服务端定义，服务端凭 reqid 自行对账。
BR 无状态：不记已发命令、不建待办表、不做超时对账。组播的"收齐回执"由服务端完成。

## 设备端(ESP32-H2)固件契约
1. **SRP 注册**：host full name 以 16 位小写十六进制 EUI64 开头(如 `1a2b3c4d5e6f7080`)；
   注册可路由 IPv6 地址与服务实例名。BR 靠 host 名前 16 字符匹配 EUI64。
2. **CoAP server**：监听 `MQTT_OT_BRIDGE_COAP_URI`(默认 `ctrl`)接收控制命令。
3. **组播**：加入 `MQTT_OT_BRIDGE_MULTICAST_ADDR`(默认 `ff03::1`)。
4. **上报**：
   - 对下行命令的应答：执行后单播 CoAP POST 到 BR 的 `MQTT_OT_BRIDGE_ACK_URI`(默认 `ack`)，payload 内带服务端下发的 reqid。
   - 设备主动上报事件(motion/boot/heartbeat)：单播 CoAP POST 到 BR 的 `MQTT_OT_BRIDGE_DEVUP_URI`(默认 `devup`)，payload 内带设备自生成的 reqid 与 event 字段。
5. **组播抖动**：收到组播命令后随机延迟 0~500ms 再上报，避免响应风暴。

## 数据流
```
服务端 --MQTT/MQTTS(登陆)--> BR --CoAP--> H2 设备
  cmd/unicast/<eui64>  ->  NON 单播  -> 设备执行 -> 响应 -> /ack -> cmd/resp
  cmd/multicast        ->  NON ff03::1 -> 各设备执行 -> 单播到 /ack -> cmd/resp
  设备主动上报         ->  /devup -> dev/up
  (BR 定期遍历 SRP)    ->  dev/registry (retained)
```
