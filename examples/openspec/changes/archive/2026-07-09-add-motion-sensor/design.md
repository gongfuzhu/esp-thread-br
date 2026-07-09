## Context

`deep_sleep` 是 Thread SED(Sleepy End Device)示例:设备到 CHILD 后进深睡,靠 RTC 定时器或 GPIO 唤醒。深睡=**掉电+重启**,每次唤醒都是冷启动、丢失 Thread/SRP 状态,必须重新 attach + 重新 SRP 注册。本 change 让唤醒承载"上报一个事件"的语义,事件类型由唤醒原因决定。

上报链路与 BR 侧的硬约定已由 `mqtt_ot_bridge`(见 [[mqtt-ot-bridge]] 记忆)固定:设备单播 NON POST 到 BR 的 `/ack` CoAP 资源,BR 原样透传 payload 到 MQTT `<prefix>/dev/response`。BR 不解析 payload,故新增字段零成本。

## Goals / Non-Goals

**Goals**
- 运动传感器 3.3V 高电平 → EXT1 唤醒 → 上报 `event:"motion"`。
- 保留定时唤醒作为心跳 → 上报 `event:"heartbeat"`。
- 上报成功后才回深睡;失败有兜底,不无限清醒。

**Non-Goals**
- 不改 BR / `mqtt_ot_bridge`(payload 透传)。
- 不做服务端下行控制(本示例是纯上报传感器,不监听 `/ctrl`)。
- 不追求毫秒级延迟(那是 light_sleep 的场景);深睡每次重入网,秒级延迟是可接受代价。
- 不引入 cJSON 托管组件。

## Decisions

### 决策 1:睡眠触发从"固定 5 秒"改为"报文发出后"
现状 `ot_state_change_callback` 在首次到 CHILD 时启动一个 5 秒单次定时器就进深睡。问题:5 秒内 SRP 可能还没注册完,`otSrpClientGetServerAddress` 返回 NULL,`device_report` 直接丢弃 → 事件永远发不出。

**方案**:睡眠不再由固定定时器驱动,而是由**上报完成**驱动:
- 监听 SRP 客户端状态,注册成功(拿到 server 地址)后执行一次上报。
- 上报的 CoAP 请求用带响应回调的发送(或发送后短暂等待 flush),确认报文已交给 radio 后 `esp_deep_sleep_start()`。
- 另设一个**最大清醒兜底定时器**(Kconfig,默认如 10s):无论成功与否,到点强制回深睡,防止 attach 失败时空耗电。

```
 boot → 读唤醒原因(定 event)→ attach
   │
   ├─ SRP 注册成功回调 ──▶ device_report(event) ──▶ 发送完成 ──▶ deep_sleep_start()
   │
   └─ 最大清醒兜底定时器到期 ──────────────────────────────▶ deep_sleep_start()
```

### 决策 2:唤醒原因 → event 映射
复用示例已有的 `esp_sleep_get_wakeup_causes()`:
- `BIT(ESP_SLEEP_WAKEUP_EXT1)` → `"motion"`
- `BIT(ESP_SLEEP_WAKEUP_TIMER)` → `"heartbeat"`
- `BIT(ESP_SLEEP_WAKEUP_UNDEFINED)`(上电首启)→ `"boot"`

在 `ot_deep_sleep_init()` 读原因阶段确定 event,存到静态变量供上报使用。

### 决策 3:EXT1 运动唤醒配置
- GPIO8(H2 EXT1 合法范围 8–14,GPIO7 虽是 RTC 但未引出;GPIO9 已被 BOOT 占用)。
- `esp_sleep_enable_ext1_wakeup_io(1ULL<<8, ESP_EXT1_WAKEUP_ANY_HIGH)`(现状是 ANY_LOW 按钮,须翻转)。
- 定时唤醒 `esp_sleep_enable_timer_wakeup` 保留,两个源并存,靠唤醒原因区分。
- 推挽传感器无需外部电阻;深睡 RTC 外设掉电、内部上下拉不保持,若传感器空闲会浮空则需外部下拉(文档说明,非代码)。

### 决策 4:上报用手写 JSON,自生成 reqid
本示例无服务端命令、无 cJSON。payload 字段全已知,用 `snprintf` 拼:
`{"id":"<eui64>","reqid":"<reqid>","event":"<event>"}`。
`reqid` 由设备自生成(如 `esp_random()` 十六进制),满足服务端凭 reqid 对账的约定。EUI64→字符串复用 `device_eui64.c/.h`。

### 决策 5:SRP 重注册
每次深睡唤醒都重跑 SRP 注册。BR 对同名 host/EUI64 重注册是刷新租约,可容忍。注册逻辑从 `ot_iot_device/main/iot_device.c` 的 `srp_register` 移植(去掉 service 的 port 若不监听 CoAP server 也可保留,便于 BR registry 选路)。

## Risks / Trade-offs

- **延迟**:每次运动都要重 attach + 重 SRP 注册,事件从触发到落 MQTT 约数秒。已与用户确认接受(选深睡是为极致省电)。
- **兜底时长权衡**:兜底太短可能在慢速 attach 时截断上报;太长则失败时多耗电。默认给一个偏宽的值(如 10s),Kconfig 可调。
- **连续运动**:高电平持续期间不会重复唤醒(已在睡);高电平未回落时再次进深睡会立即被 ANY_HIGH 再唤醒 → 可能连报。可接受(等价于"持续有人"周期上报),必要时后续加去抖。

## Migration Plan

新增示例行为,无迁移。硬件需按接线说明接传感器到 GPIO8。

## Open Questions

- 是否要在 SRP service 里保留 CoAP server 端口(本示例不监听下行)?保留可让 BR registry 正常收录该设备,倾向保留。
