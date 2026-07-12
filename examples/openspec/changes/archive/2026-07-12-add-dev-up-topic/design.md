## Context

BR 的 `mqtt_ot_bridge` 目前只有一个 CoAP 上行资源 `ack`,其回调 `ack_request_handler` 无条件调用 `publish_uplink()`,而 `publish_uplink` 把 topic 写死为 `<prefix>/cmd/resp`(`mqtt_ot_bridge.c:67-73`)。三条上行流(IoT 组件命令应答、单播 CON 回执、deep_sleep 主动事件)共用同一 URI `ack`,全部落到 `cmd/resp`。deep_sleep 的 motion 报文还是旧字段 `{id,reqid,event}`(`esp_ot_sleepy_device.c:189`),与 IoT 组件的 `{reqid,eui64,...}` 信封不一致。

约束:BR 设计原则是"纯管道,不解析 payload";CoAP URI path 段不含 `/`;deep_sleep 手写 JSON、不使用 cJSON 托管组件;无主机端测试,验证靠编译 + 设备 monitor。

## Goals / Non-Goals

**Goals:**
- BR 按 **CoAP URI** 将主动上报分流到 `dev/up`,命令应答仍走 `cmd/resp`,BR 不解析 payload。
- deep_sleep 上报统一为 `{reqid,eui64,event,data}` 信封,与 IoT 组件同构。
- URI 与 topic 均可经 Kconfig 配置,默认 `devup` / `dev/up`。

**Non-Goals:**
- 不引入周期性上报框架、report_freq、NVS(此前已推迟,仍推迟)。
- 不改 IoT 组件(`iot_device_core` 等)的命令应答路径——继续走 `ack`→`cmd/resp`。
- 不做新旧固件热兼容;BR 与 sleepy device 配对升级。
- 不动 `otbr/dev/registry`(retained 快照)与 `cmd/registry` 应答路径。

## Decisions

### D1：按 CoAP URI 分流(方案 A),而非嗅探 payload
BR 新增第二个 CoAP 资源 `devup`,其回调发布到 `dev/up`;`ack` 回调发布到 `cmd/resp`。把 `publish_uplink(payload,len)` 重构为 `publish_uplink_to(suffix, payload, len)`,两个 handler 传各自的 topic 后缀。
- **为何**:保持 BR 纯管道,不给每条上行加 JSON 解析开销,event 名单不硬编码进 BR;与既有 `cmd/registry` 专用 topic 的"按通道分流"思路一致。
- **替代**:方案 B(BR cJSON 解析 `event` 字段路由)——破坏纯管道约定、加解析开销、event 名单要维护在 BR,已否决。

### D2：deep_sleep 上报统一新信封
`device_report` 的 snprintf 由 `{"id":...,"reqid":...,"event":...}` 改为 `{"reqid":...,"eui64":...,"event":...,"data":{}}`。字段 `id`→`eui64`,补空 `data` 占位。
- **为何**:两条上行流字段同构,后端只维护一套解析;`data` 占位为将来带 payload 的事件(如传感器读数)预留位置。
- **替代**:只搬 topic 不改格式——省 deep_sleep 改动,但 `dev/up` 上留旧字段 `id`,后端要认两套,已否决。

### D3：URI/topic 经 Kconfig,默认 devup / dev/up
BR 新增 `MQTT_OT_BRIDGE_DEVUP_URI`(default `"devup"`);上行 topic 后缀 `dev/up` 直接在代码内固定(与 `cmd/resp` 同风格,当前 topic 后缀均为代码常量,仅前缀可配)。deep_sleep 的 `MOTION_ACK_URI` 默认由 `"ack"` 改 `"devup"`(名字保留 `MOTION_ACK_URI` 以减少改动面,仅改默认值与注释)。
- **为何**:CoAP URI 不含 `/`,故 URI 用 `devup`、MQTT topic 用 `dev/up`;沿用现有"前缀可配、后缀固定"的约定。

## Risks / Trade-offs

- [固件版本错配]→ 旧 deep_sleep(发 `ack`)配新 BR:motion 掉进 `ack`→`cmd/resp`;新 deep_sleep 配旧 BR:BR 无 `devup` 资源收不到 → 在 proposal/README 标注为**配对升级**,非热兼容。
- [`MOTION_ACK_URI` 名字与新语义不符]→ 名字仍叫 `ACK` 但默认指向 `devup`,可能误导 → 在 Kconfig help 文本注明"上报目标为 BR 的 devup 资源"。
- [第二个 CoAP 资源生命周期]→ `coap_ensure_started` 里需 `otCoapAddResource` 两个资源,且资源 struct 为 static 存续 → 复用现有 `s_ack_resource` 模式新增 `s_devup_resource`,同一把 OT 锁内注册。
- [无主机测试]→ 改动均为字符串/资源注册,靠编译 + monitor 验证:BR 端看 `devup` 资源日志与 `dev/up` 发布,设备端看新信封。

## Migration Plan

1. 改 BR:参数化上行 + 新增 `devup` 资源 + Kconfig。
2. 改 deep_sleep:信封字段 + `MOTION_ACK_URI` 默认。
3. 同版本编译并配对刷写 BR 与 sleepy device。
4. 回滚:整体回退本 change 的两端改动即可(两端 URI 同时回 `ack`)。

## Open Questions

无。三个岔口(URI=devup、信封统一、命令应答留 cmd/resp)已在探索阶段确认。
