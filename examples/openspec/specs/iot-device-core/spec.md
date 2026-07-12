# iot-device-core Specification

## Purpose
TBD - created by archiving change add-iot-device-components. Update Purpose after archive.
## Requirements
### Requirement: SRP 自动注册
内核 SHALL 在 Thread 连通后以本机 16 位小写十六进制 EUI64 作为 SRP host 名自动注册服务，service 类型与端口可经 Kconfig 配置。注册 SHALL 使用 SRP 客户端 auto-start 模式。

#### Scenario: 注册服务
- **WHEN** 设备加入 Thread 网络并发现 SRP server
- **THEN** 内核以 EUI64 为 host/instance 名注册配置的 service 类型，供 BR 侧发现与选路

### Requirement: CoAP 传输与组播订阅
内核 SHALL 启动 CoAP server 并注册下行控制资源（Uri-Path 可配，默认 `ctrl`），同时订阅配置的组播地址（默认 `ff03::1`），以接收单播与组播下行指令。

#### Scenario: 接收单播指令
- **WHEN** BR 向本机 IPv6 的 `ctrl` 资源发送 CoAP 请求
- **THEN** 内核读取 payload 并进入信封解析与分发

#### Scenario: 接收组播指令
- **WHEN** BR 向 `ff03::1` 发送 CoAP NON 请求
- **THEN** 已订阅该组播地址的设备均收到并各自处理

### Requirement: 命令 worker 任务
内核 SHALL 创建独立的命令 worker 任务处理下行指令。CoAP 接收回调（OpenThread 任务上下文）SHALL 仅解析信封并把 `{reqid, event, data, 是否组播, peer}` 投递到队列后立即返回，MUST NOT 在 CoAP 回调中执行 handler 或阻塞操作。worker 任务 SHALL 从队列取出后查分发表、调用 handler、封装响应并上报。handler SHALL 运行于 worker 任务上下文，允许阻塞（如 ADC 采样、LEDC 配置），并 SHALL 在需要访问 OpenThread API（如 CoAP 上报）时自行获取 OT 锁。worker 任务栈深度 SHALL 可经 Kconfig 配置。

#### Scenario: CoAP 回调不阻塞 OT 任务
- **WHEN** 内核在 CoAP 回调收到下行指令
- **THEN** 回调仅入队并返回，handler 的执行在 worker 任务中进行，不阻塞 OpenThread 任务

#### Scenario: handler 可执行阻塞操作
- **WHEN** 某 handler 执行毫秒级阻塞的硬件操作（如 ADC 采样）
- **THEN** 该阻塞发生在 worker 任务上下文，OpenThread 协议栈不受影响

#### Scenario: 队列满
- **WHEN** worker 队列已满时又到达新指令
- **THEN** 内核丢弃该指令并记录日志，不崩溃、不阻塞 CoAP 回调

### Requirement: event 分发注册表
内核 SHALL 维护 `event 名 → handler` 分发表，并提供注册接口 `iot_device_register_handler(event, handler)`。worker 任务取出指令后 SHALL 按 `event` 查表调用对应 handler；handler 签名 SHALL 为 `int (*)(const cJSON *data, cJSON *resp_data)`，返回全局状态码。

#### Scenario: 分发到已注册 handler
- **WHEN** 内核收到 `event:"switch"` 且该 event 已注册 handler
- **THEN** 内核以下行 `data` 调用该 handler，并将 handler 填入的 `resp_data` 与返回码封装为响应

#### Scenario: 未注册事件
- **WHEN** 内核收到未在分发表中的 `event`
- **THEN** 内核不调用任何 handler，直接返回 `code:-3` 响应

### Requirement: 响应信封封装与上报
内核 SHALL 负责全部协议信封：透传下行 `reqid`、填入本机 `eui64`、拼 `event+"_resp"`、据 handler 返回码填 `code` 与 `msg`。应答 SHALL 通过单播 NON CoAP POST 上报到 BR 的上报资源（Uri-Path 可配，默认 `ack`）。能力 handler MUST NOT 自行构造信封或发送上报。

#### Scenario: handler 只填业务数据
- **WHEN** handler 返回 0 并填好 `resp_data`
- **THEN** 内核封装 `{"reqid","eui64","event":"..._resp","code":0,"msg":"success","data":<resp_data>}` 并单播 NON 上报到 BR

#### Scenario: 组播指令上报加抖动
- **WHEN** 指令经组播下发（本地收包地址为多播）
- **THEN** 内核在 0~500ms 随机抖动后再上报，抖动经 FreeRTOS 单次定时器承载，MUST NOT 在 CoAP 回调中 `vTaskDelay` 阻塞 OpenThread 任务

