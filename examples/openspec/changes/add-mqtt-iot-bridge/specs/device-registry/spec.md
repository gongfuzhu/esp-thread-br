## ADDED Requirements

### Requirement: 基于 SRP 的设备发现
BR SHALL 通过遍历 OpenThread SRP server 注册表(`otSrpServerGetNextHost` / `otSrpServerHostGetNextService`)发现网内设备,提取每个设备的 EUI64、IPv6 地址与服务信息。

#### Scenario: 遍历 SRP 表
- **WHEN** BR 需要生成设备清单或解析单播目标
- **THEN** BR 遍历 SRP server 注册的 host/service,得到 `{eui64, ipv6, service}` 记录集合

#### Scenario: SRP server 未启用
- **WHEN** `CONFIG_OPENTHREAD_SRP_SERVER` 未启用
- **THEN** BR 记录警告,设备清单为空,单播选路不可用(文档中列为前置条件)

### Requirement: 设备清单上报
BR SHALL 周期性地将 SRP 表导出的设备清单发布到上行清单 topic(`dev/registry`),使用 MQTT retained 标志。刷新周期 SHALL 可经 Kconfig 配置。

#### Scenario: 周期性发布清单
- **WHEN** 到达配置的刷新周期
- **THEN** BR 遍历 SRP 表并将 `[{eui64, ipv6, service}...]` 以 retained 消息发布到 `dev/registry`

#### Scenario: 新订阅者获取当前清单
- **WHEN** 服务端在任意时刻订阅 `dev/registry`
- **THEN** 因 retained 标志,服务端立即收到最近一次发布的设备清单

### Requirement: EUI64 到 IPv6 的选路解析
BR SHALL 使用 SRP 表作为单播命令的 `EUI64 → IPv6` 解析来源;该表用于清单上报与选路,但 SHALL NOT 用作对账账本。

#### Scenario: 选路查询
- **WHEN** 单播命令需要把 `<eui64>` 转为可路由 IPv6
- **THEN** BR 在 SRP 表中查找该 EUI64 对应的已注册 IPv6 地址并用于 CoAP 目标
