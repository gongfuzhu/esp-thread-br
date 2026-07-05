## Context

Thread BR 主机固件(ESP32-C6/C5/P4 + 外接 ESP32-H2 RCP)当前只做网络层工作(路由、mDNS、SRP server、前缀下发),不理解应用语义。用户需从局域网 MQTT 客户端(手机/PC,称"服务端")控制自研的 ESP32-H2 IoT 设备(设备端固件由用户自己编写),支持单播/组播,并获取执行反馈与在线设备清单。

**关键约束(来自探索阶段的决策):**
- 设备端应用协议 = **CoAP over IPv6**(Thread 原生,OpenThread 内置 `otCoap*` API,已在 `coap.h` 确认)。
- Broker 在局域网内一台机器,**用户名/密码登陆,不上 TLS**。
- 组播 = **全网 realm-local `ff03::1`**(不做分组)。
- 设备 ID = **EUI64**;服务端持有 `EUI64 ↔ 友好名` 映射表。
- 每条命令由**服务端生成唯一 reqid**,设备执行后原样回传;**对账在服务端**完成。
- BR **透传** payload,不理解语义。

## Goals / Non-Goals

**Goals:**
- BR 作为**无状态双向管道**:MQTT 下行 → CoAP;CoAP 上行 → MQTT。
- 支持单播(CoAP CON,响应随事务返回)与组播(CoAP NON → `ff03::1`)。
- BR 注册一个 CoAP server 资源(`/ack`)统一接收设备主动上报的响应/状态。
- 基于 SRP server 注册表(`otSrpServerGetNextHost/Service`)周期性上报设备清单,并作为单播选路的 `EUI64 → IPv6` 查询来源。
- 所有配置经 Kconfig(broker 地址/端口、用户名/密码、topic 前缀、清单刷新周期)。

**Non-Goals:**
- BR **不**理解 payload 语义(不解析 on/off/亮度)。
- BR **不**做对账、不维护待办表、不跑超时定时器、不生成 reqid。
- BR **不**管理设备分组。
- **不**做 TLS/云接入(局域网测试阶段)。
- **不**提供设备端 H2 固件(仅约定其必须遵守的行为)。

## Decisions

### D1. BR 无状态,对账全交服务端
服务端生成 reqid、持有映射表,天然能判断"发给谁、收到哪些"。BR 若兜底对账,分母只能是"SRP 全表"这种粗粒度,报出的 missing 是一堆 EUI64,服务端仍需翻译——价值低且让 BR 变重。
- **选择**:BR 收到任何 CoAP 响应就透传到 MQTT,不勾选、不超时。
- **替代**:BR 按 SRP 清单对账 → 否决(BR 变有状态,收益低)。

### D2. 两条上行路径,统一为"透传到 MQTT"
```
单播 (CON):  BR 发 CON → 设备 ACK+响应【同一 CoAP 事务】→ 回调透传 MQTT
组播 (NON):  BR 发 NON → ff03::1(发完即忘)
             设备各自单播新 CoAP 请求 → BR 的 /ack 资源 → 回调透传 MQTT
```
两条路的回调逻辑几乎一致:拿到 payload,原样 publish。reqid 在 payload 内,BR 不解析。

### D3. `/ack` 作为组播响应的统一回流入口
组播的 NON 请求没有 CoAP 层响应通道,设备的响应是**独立新消息**。约定设备把响应单播到 BR 的固定 CoAP 资源 `/ack`,BR `otCoapAddResource` 注册该资源,收到即透传。这是 BR 唯一"被动收包"入口。

### D4. SRP 表的双重角色:清单 + 选路(非账本)
BR 为 `dev/registry` 遍历 SRP 时,顺手得到 `EUI64 → IPv6`。单播命令用此表把 `<eui64>` 解析为可路由 IPv6。SRP 仍**不是**对账账本。
- **替代**:设备用 EUI64 生成确定性 IPv6,BR 直接算 → 否决(耦合地址格式,而 SRP 遍历本就要做)。

### D5. Topic 结构
```
下行:  cmd/unicast/<eui64>    payload 透传进 CoAP CON → 该设备
       cmd/multicast          payload 透传进 CoAP NON → ff03::1
上行:  dev/response           BR 把每条收到的 CoAP 响应 payload 原样发布
清单:  dev/registry           BR 定期遍历 SRP 发布 [{eui64,ipv6,service}...] (retained)
```
上行统一到单一 topic(BR 不区分内容);是否按类型细分由服务端订阅偏好决定,BR 无所谓。

### D6. 组件形态
新增可复用组件 `common/mqtt_ot_bridge`(参照 `common/thread_border_router` 的组织方式:`include/` + `src/` + `Kconfig.projbuild`)。`basic_thread_border_router/main/esp_ot_br.c` 在 BR 启动后调用一次 `mqtt_ot_bridge_start()`。依赖 `espressif/mqtt`(esp-mqtt,IDF 官方组件;`components/mqtt/` 当前为未 checkout 的空 submodule,实现阶段需验证 `mqtt_client.h` 可用)。

## Risks / Trade-offs

- **[CoAP 多播必须 NON,无 ACK]** → 收齐回执由服务端应用层对账,已在架构中前置消化(D1)。
- **[组播响应风暴]** 全网 `ff03::1` 一发,设备几乎同时单播回 BR,再涌向 MQTT,设备多时短时拥塞 → 设备端约定收到组播后**随机抖动 0~500ms** 再响应(设备固件责任,此处约定)。
- **[esp-mqtt 组件未 checkout]** `components/mqtt/` 仅剩 `test_apps` → 实现时通过 `idf_component.yml` 声明 `espressif/mqtt` 依赖并验证编译。
- **[SRP server 未启用则无清单/选路]** → 需确保 `CONFIG_OPENTHREAD_SRP_SERVER` 开启;文档中明确前置条件。
- **[设备未运行 SRP 客户端]** BR 无法得知其 IPv6,单播不可达 → 约定设备端必须跑 SRP 客户端注册(instance/host 带 EUI64 与地址)。
- **[无主机端测试]** 遵循仓库现状,验证方式为编译 + 设备端 monitor。

## Migration Plan

新增功能,无破坏性变更。BR 启动流程仅多一次 `mqtt_ot_bridge_start()` 调用;不启用(Kconfig 关闭)时行为与现状完全一致。回滚 = 关闭 Kconfig 选项或移除该调用。

## Open Questions

- 抖动上限 500ms 是否合适,是否需随 SRP 表规模动态调整?(设备固件侧,暂定 500ms)
- `dev/registry` 刷新周期默认值(暂定周期性 + SRP 表变更时触发?),以及是否需要 MQTT LWT 标记 BR 离线。
- 上行是否细分 `dev/response` / `dev/state`(BR 无所谓,取决于服务端)。
