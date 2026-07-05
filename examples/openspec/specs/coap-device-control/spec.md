# coap-device-control Specification

## Purpose

定义 BR 侧 CoAP 客户端与服务端行为，用于向 Thread 网内设备下发控制命令并接收响应。单播使用 CON 请求 + 事务响应回调；组播使用 NON 请求且不做对账；BR 额外提供 `/ack` 资源用于设备主动上报。转发层保持无状态，payload 原样透传。

## Requirements

### Requirement: 单播 CoAP 控制
BR SHALL 通过 CoAP CONfirmable 请求向单个 Thread 设备下发控制命令,目标 IPv6 由 EUI64 经 SRP 表解析获得。

#### Scenario: 目标设备在 SRP 表中
- **WHEN** BR 收到面向某 `<eui64>` 的单播命令,且该 EUI64 在 SRP 表中有对应 IPv6
- **THEN** BR 向该 IPv6 发送 CoAP CON 请求,payload 透传

#### Scenario: 目标设备不在 SRP 表中
- **WHEN** `<eui64>` 无法在 SRP 表中解析到 IPv6
- **THEN** BR 记录错误日志并丢弃该命令,不崩溃、不阻塞后续命令

#### Scenario: 单播响应回流
- **WHEN** 设备对 CON 请求返回响应(同一 CoAP 事务)
- **THEN** BR 在响应回调中把 payload 原样发布到上行 topic

### Requirement: 组播 CoAP 控制
BR SHALL 通过 CoAP NON-confirmable 请求向 `ff03::1`(realm-local all-nodes)下发组播命令,发送后不等待、不重传、不对账。

#### Scenario: 发送组播命令
- **WHEN** BR 收到组播命令
- **THEN** BR 向 `ff03::1` 发送单条 CoAP NON 请求后即返回,不维护待办表或超时定时器

### Requirement: 设备主动上报接收资源
BR SHALL 注册一个固定的 CoAP server 资源(`/ack`),接收设备(尤其是组播命令的被控设备)主动单播上报的响应/状态。

#### Scenario: 收到设备上报
- **WHEN** 设备向 BR 的 `/ack` 资源单播 CoAP 请求
- **THEN** BR 接收该 payload 并原样发布到上行 topic,按需返回 CoAP ACK

### Requirement: 无状态转发
BR SHALL NOT 记录已发命令、维护待办清单、跟踪 reqid 或按名单对账;每条上行 CoAP payload 独立透传。

#### Scenario: 组播响应独立透传
- **WHEN** 多个设备因一条组播命令各自上报响应
- **THEN** BR 逐条透传到上行 topic,不聚合、不判断是否收齐、不报告缺失设备
