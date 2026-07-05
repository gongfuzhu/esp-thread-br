## Why

Thread BR 目前只做网络层的活(路由、mDNS、SRP、前缀下发),不理解"开灯/关锁"这类应用语义。用户需要从局域网 MQTT 客户端(手机/PC)控制 Thread 网内的自研 ESP32-H2 IoT 设备,并拿到执行反馈和在线设备清单。这需要在 BR 主机固件上引入一个 **MQTT ↔ CoAP 翻译层**。

## What Changes

- 新增 MQTT 客户端(esp-mqtt),连接局域网内自建 broker,使用**用户名/密码登陆**(局域网不上 TLS)。
- 新增 **MQTT ↔ OpenThread CoAP 桥接**:把 MQTT 下行 topic 翻译为对 Thread 设备的 CoAP 请求。
  - **单播控制**:CoAP CON(confirmable),设备回 ACK+响应,天然收回执。
  - **组播控制**:CoAP NON 发到 Thread realm-local 多播地址;设备各自单播 CoAP 响应回 BR;BR 按 SRP 清单**逐个对账**,超时后报告缺失设备(应用层收齐回执,非 CoAP 层)。
- 新增**设备清单上报**:遍历 SRP server 注册表(`otSrpServerGetNextHost/Service`)得到"设备ID + IPv6 + 服务类型",发布到 MQTT(retained)。
- 新增**执行反馈上报**:CoAP 响应回调 → 发布 `ack/<devid>` 与 `state/<devid>`。
- 新增 Kconfig 选项:broker 地址/端口、用户名/密码、topic 前缀、多播地址、组播对账超时。

## Capabilities

### New Capabilities
- `mqtt-bridge`: MQTT 客户端接入(登陆、连接管理、订阅/发布)与 topic ↔ 动作的翻译约定。
- `coap-device-control`: 经 OpenThread CoAP 向 Thread 设备下发单播/组播控制并收集响应(含组播对账机制)。
- `device-registry`: 基于 SRP server 注册表的网内设备发现与清单上报。

### Modified Capabilities
<!-- 无现有 spec 的需求变更;BR 启动流程仅新增一次桥接初始化调用,不改变既有需求。 -->

## Impact

- **代码**:新增可复用组件 `common/mqtt_ot_bridge`(或 `examples/mqtt_thread_border_router`);`basic_thread_border_router/main/esp_ot_br.c` 在 BR 启动后调用一次桥接初始化。
- **依赖**:`espressif/mqtt`(esp-mqtt,IDF 官方组件)。OpenThread CoAP/SRP server API(IDF 内置,已确认可用)。
- **配置**:需确保 BR 侧 SRP server 已启用(`OPENTHREAD_SRP_SERVER`);设备侧需运行 SRP 客户端注册 + CoAP server + 加入多播组。
- **约束**:CoAP 规范要求多播请求为 NON-confirmable,因此"收齐组播回执"必须由应用层对账实现,不能依赖 CoAP ACK。
