# MQTT 对接文档(后端接入)

本文档面向**后端/服务端**开发者,描述如何通过局域网 MQTT broker 与 Thread Border
Router(BR)交互,进而控制 Thread 网内的 ESP32-H2 设备、接收设备上报。

> BR 是**无状态透传管道**:它不解析 payload、不生成 ID、不做对账。所有业务语义
> (命令内容、`reqid` 对账、组播收齐判断)都由**后端负责**。BR 只在 MQTT 与
> Thread CoAP 之间搬运字节。

---

## 1. 连接参数

| 项 | 默认值 | 说明 |
|----|--------|------|
| Broker 地址 | `mqtt://192.168.1.100:1883` | 局域网,无 TLS。由 BR 固件 `MQTT_OT_BRIDGE_BROKER_URI` 配置 |
| 用户名 | `bridge` | `MQTT_OT_BRIDGE_USERNAME` |
| 密码 | `changeme` | `MQTT_OT_BRIDGE_PASSWORD`,**上线前务必修改** |
| Topic 前缀 | `otbr` | `MQTT_OT_BRIDGE_TOPIC_PREFIX`,下文用 `<prefix>` 代指 |

后端作为一个普通 MQTT 客户端连接同一个 broker 即可。BR 也是该 broker 的客户端,
两者通过 topic 约定通信,彼此不直接连接。

```
   后端/服务端                    MQTT broker                    BR (ESP32-C6)
   ──────────                    ───────────                    ─────────────
   publish cmd/*    ───────────▶              ───────────────▶  订阅 cmd/*,翻译成 CoAP
   订阅 dev/*       ◀───────────              ◀───────────────  publish dev/*
```

---

## 2. Topic 总表

`<prefix>` 默认为 `otbr`。`<eui64>` 为设备的 16 位小写十六进制 EUI64(设备唯一 ID)。

| 方向 | Topic | QoS | Retained | 谁发 | 谁收 | 说明 |
|------|-------|-----|----------|------|------|------|
| 下行·单播 | `<prefix>/cmd/unicast/<eui64>` | 0 | 否 | 后端 | BR | 向指定设备发命令 |
| 下行·组播 | `<prefix>/cmd/multicast` | 0 | 否 | 后端 | BR | 向全部设备广播命令 |
| 上行·响应 | `<prefix>/dev/response` | 0 | 否 | BR | 后端 | 设备的响应/主动上报,原样透传 |
| 设备清单 | `<prefix>/dev/registry` | 0 | **是** | BR | 后端 | 在线设备列表,retained + 周期刷新(默认 30s) |

**后端需要做的**:
- **订阅** `<prefix>/dev/response` 和 `<prefix>/dev/registry`
- **发布**到 `<prefix>/cmd/unicast/<eui64>` 或 `<prefix>/cmd/multicast`

---

## 3. 设备清单 `dev/registry`

后端连上 broker 后,由于该 topic 是 **retained**,会**立即收到**最近一次的设备清单
(无需等待)。之后 BR 每 30 秒刷新一次。

**Topic**: `otbr/dev/registry`
**Payload**(JSON 数组):

```json
[
  {
    "eui64": "744dbdfffe664fc4",
    "ipv6": "fd00:db8:0:0:0:ff:fe00:b401",
    "service": "_iot._udp"
  },
  {
    "eui64": "1a2b3c4d5e6f7080",
    "ipv6": "fd00:db8:0:0:abcd:ef01:2345:6789",
    "service": "_iot._udp"
  }
]
```

| 字段 | 含义 |
|------|------|
| `eui64` | 设备唯一 ID,用于拼单播 topic |
| `ipv6` | 设备当前可路由 IPv6(仅供参考,后端选路用 `eui64` 即可,BR 内部做 EUI64→IPv6) |
| `service` | SRP 服务类型名,用于区分设备种类(如 `_iot._udp`) |

> **注意**:清单只含**跑了 SRP 注册的设备**(即本项目的 `ot_iot_device` / `deep_sleep`)。
> 纯 `ot_cli` 或其他 router 不会出现,这是设计使然。空网络时 payload 为 `[]`。
> **深睡设备**大部分时间在睡眠,可能不在清单里或 `ipv6` 已过期——不要依赖清单判断
> 深睡设备是否"在线"。

---

## 4. 下行命令(后端 → 设备)

### 4.1 单播:控制单个设备

**Topic**: `otbr/cmd/unicast/<eui64>`
**Payload**: 由后端定义,BR 原样透传给设备。本项目 `ot_iot_device` 约定如下:

```json
{ "reqid": "req-0001", "cmd": "on" }
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `reqid` | string | **后端生成的请求 ID**,设备原样回传,后端凭此对账 |
| `cmd` | string | 命令:`on` / `off` / `query` / `blink`(见下表) |

设备端 `ot_iot_device` 支持的 `cmd`:

| cmd | 行为 |
|-----|------|
| `on` | 打开开关/LED(持续) |
| `off` | 关闭 |
| `query` | 不改变状态,仅回报当前状态 |
| `blink` | 点亮后固定时长自动熄灭(瞬时脉冲,用于寻址确认) |

**示例**:让设备 `744dbdfffe664fc4` 亮灯
```
Topic:   otbr/cmd/unicast/744dbdfffe664fc4
Payload: {"reqid":"req-0001","cmd":"on"}
```

### 4.2 组播:广播给所有设备

**Topic**: `otbr/cmd/multicast`
**Payload**: 同上格式,BR 通过 CoAP 组播发给所有设备。

```
Topic:   otbr/cmd/multicast
Payload: {"reqid":"batch-0007","cmd":"off"}
```

组播下每个设备都会**各自单播回执**到 `dev/response`(带随机 0~500ms 抖动避免风暴)。
后端会收到**多条**响应,需凭 `reqid` + `id` 自行判断"收齐了几台"。

---

## 5. 上行响应/上报(设备 → 后端)

所有设备的响应和主动上报,BR 都原样发布到**同一个** topic:

**Topic**: `otbr/dev/response`

### 5.1 控制设备的响应(`ot_iot_device`)

命令执行后的回执:

```json
{ "id": "744dbdfffe664fc4", "reqid": "req-0001", "state": "on" }
```

| 字段 | 说明 |
|------|------|
| `id` | 上报设备的 EUI64 |
| `reqid` | **原样回传**后端下发的 reqid;后端凭 `id`+`reqid` 对账 |
| `state` | 当前状态:`on` / `off` |
| `action` | (可选)动作标识,如 blink 命令回执带 `"action":"blink"` |

blink 回执示例(固定 `state:"off"` + `action:"blink"`):
```json
{ "id": "744dbdfffe664fc4", "reqid": "req-0002", "state": "off", "action": "blink" }
```

### 5.2 深睡传感器设备的主动上报(`deep_sleep`)

深睡设备**不接收下行命令**,只在被硬件事件/心跳唤醒后主动上报。payload 带 `event`
字段,`reqid` 由**设备自生成**(无对应的下行命令):

```json
{ "id": "744dbdfffe664fc4", "reqid": "8810a612", "event": "motion" }
```

| 字段 | 说明 |
|------|------|
| `id` | 设备 EUI64 |
| `reqid` | 设备自生成的 8 位十六进制随机值(每次上报唯一,用于去重) |
| `event` | 事件类型:`motion`(运动触发) / `heartbeat`(周期心跳) / `boot`(首次上电) |

> **区分设备类型**:后端可凭 payload 有无 `event` 字段区分"传感器上报"与"控制回执";
> 或结合 `dev/registry` 的 `service` 字段。

### 上行·主动上报 `<prefix>/dev/up`

设备**主动**上报的事件(motion/boot/heartbeat 等,无对应下行命令)由 BR 的
`devup` CoAP 资源接收后原样发布到此 topic。与 `cmd/resp`(对下行命令的应答)
按 **CoAP 接收资源** 分流,BR 不解析 payload。

信封(与 IoT 组件同构):

```json
{ "reqid": "e2664224", "eui64": "744dbdfffe664fc4", "event": "motion", "data": {} }
```

| 字段 | 含义 |
|---|---|
| `reqid` | 设备自生成的 8 位十六进制随机值(每次上报唯一,用于去重) |
| `eui64` | 设备唯一 ID(16 位小写 hex) |
| `event` | 事件类型:`motion` / `boot` / `heartbeat` |
| `data` | 事件数据对象(当前为空占位,预留传感器读数等) |

> topic 语义划分:`cmd/resp` = 对下行命令的应答(带 `code`/`msg`);
> `dev/up` = 设备主动上报(无对应下行命令)。

---

## 6. reqid 对账(后端职责)

BR **不管** reqid。完整对账逻辑在后端:

1. 下发单播命令时,后端生成唯一 `reqid`,记入待办表。
2. 收到 `dev/response`,用 `id`+`reqid` 匹配待办,标记完成。
3. 超时未收到响应 → 后端自行判定失败/重试(BR 不重传、不超时通知)。
4. 组播:后端下发一个 `reqid`,预期收到 N 条带同 `reqid`、不同 `id` 的响应,
   自行统计"收齐/缺哪台"。

```
后端                          BR                          设备
 │  cmd/unicast/<eui64>        │                            │
 │  {reqid:"req-0001",cmd:on}  │                            │
 ├───────────────────────────▶│  CoAP → ctrl               │
 │  (记待办 req-0001)          ├───────────────────────────▶│ 执行 on
 │                             │                            │
 │                             │  ack ← 单播上报             │
 │       dev/response          │  {id,reqid:"req-0001",     │
 │◀────────────────────────────┤   state:"on"}             │
 │  (匹配待办 req-0001 完成)   │◀───────────────────────────┤
```

---

## 7. 快速对接清单(TL;DR)

1. 连接 broker(`mqtt://<BR所在网段host>:1883`,账号见上),
2. **订阅** `otbr/dev/response` 和 `otbr/dev/registry`,
3. 连上后立即从 retained 的 `dev/registry` 拿到设备列表,取 `eui64`,
4. **控制**:publish 到 `otbr/cmd/unicast/<eui64>`,payload `{"reqid":"...","cmd":"on"}`,
5. **收响应**:在 `otbr/dev/response` 上按 `id`+`reqid` 对账,
6. **传感器**:深睡设备的 `motion`/`heartbeat` 事件也从 `otbr/dev/response` 收,凭 `event` 字段识别。

---

## 8. 备注:字段是两端约定,不是 BR 强制

`cmd` / `state` / `event` / `reqid` 这些字段是**设备固件与后端之间**的约定,BR 完全
透传。如果新增设备类型或命令,只需两端(设备固件 + 后端)对齐 payload,**BR 无需改动**。
新增契约字段时的正式流程见 `examples/ITERATION_GUIDE.md` 第 6 节(流程 B)。

相关文档:
- BR 组件配置与固件契约:`common/mqtt_ot_bridge/README.md`
- 控制设备端实现:`ot_iot_device/README.md`
- 深睡传感器设备端:`deep_sleep/README.md`
- 正式契约 spec:`openspec/specs/{mqtt-bridge,coap-device-control,device-registry,motion-event-report}/spec.md`
