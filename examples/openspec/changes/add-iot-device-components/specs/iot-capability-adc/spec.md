## ADDED Requirements

### Requirement: adc_read 事件处理
`iot_cap_adc_read` 组件 SHALL 提供 `iot_cap_adc_read_init()`，在其中向内核注册 `adc_read` event 的 handler。handler SHALL 解析 `data.channel`，经 ESP-IDF ADC 驱动采样，并在 `resp_data` 回填 `{"channel","raw_val","voltage"}`，其中 `voltage` 为经校准换算的电压值。

#### Scenario: 读取 ADC 通道
- **WHEN** handler 收到 `data:{"channel":0}`
- **THEN** 采样通道 0，返回 code 0，`resp_data` 为 `{"channel":0,"raw_val":<原始值>,"voltage":<校准电压>}`

#### Scenario: 通道非法
- **WHEN** `data.channel` 缺失或超出可用范围
- **THEN** handler 返回 code -1，不进行采样

### Requirement: ADC 校准句柄管理
组件 SHALL 在首次使用时创建并复用 ADC 校准句柄（组件级 static 状态）用于原始值→电压换算。校准不可用时 SHALL 仍返回 `raw_val`，`voltage` 可为原始值的线性估算或标记为不可用。

#### Scenario: 复用校准句柄
- **WHEN** 多次 `adc_read` 同一 ADC 单元
- **THEN** 复用已创建的校准句柄，不重复创建

#### Scenario: 校准不可用
- **WHEN** 目标 ADC 单元/衰减无可用校准方案
- **THEN** handler 仍返回 `raw_val` 与 code 0，`voltage` 采用线性估算
