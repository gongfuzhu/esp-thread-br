## ADDED Requirements

### Requirement: 统一下行指令信封
下行指令 SHALL 采用统一 JSON 信封 `{"reqid","event","data"}`。`reqid` SHALL 全局唯一用于上下行配对；`event` SHALL 标识设备执行事件；`data` SHALL 为该 event 的私有参数体（可为空对象）。主动上报类消息除外，不携带 `reqid`。

#### Scenario: 合法下行指令
- **WHEN** 设备收到 `{"reqid":"batch-0002","event":"switch","data":{"gpio":2,"action":"on"}}`
- **THEN** 设备按 `event` 分发到对应处理并使用 `data` 参数执行

#### Scenario: data 为空的查询类指令
- **WHEN** 收到 `{"reqid":"batch-0001","event":"registry_list","data":{}}`
- **THEN** 正常分发处理，不因 `data` 为空对象报错

### Requirement: 统一上行响应信封
应答类上行响应 SHALL 采用统一 JSON 信封，根节点强制携带 `eui64` 溯源字段：`{"reqid","eui64","event","code","msg","data"}`。`reqid` SHALL 与对应下行指令完全一致；`event` SHALL 为下行原生事件名加 `_resp` 后缀；`msg` SHALL 为 `success`（code=0）或 `fail`（code≠0）。

#### Scenario: 成功响应携带溯源与配对字段
- **WHEN** 设备成功执行 `event:"switch"`（下行 `reqid:"batch-0002"`）
- **THEN** 设备上报 `{"reqid":"batch-0002","eui64":"<本机EUI64>","event":"switch_resp","code":0,"msg":"success","data":{...}}`

#### Scenario: 响应事件名后缀
- **WHEN** 任意下行 `event` 为 `X`
- **THEN** 对应上行响应 `event` 为 `X_resp`

### Requirement: 全局状态码
响应 `code` SHALL 取自固定集合：`0` 执行成功、`-1` 参数错误、`-2` 设备忙/执行失败、`-3` 不支持该事件、`-4` 硬件异常。

#### Scenario: 参数错误
- **WHEN** 下行 `data` 缺少必填字段或字段非法
- **THEN** 响应 `code:-1`，`msg:"fail"`

#### Scenario: 不支持的事件
- **WHEN** 设备收到未注册 handler 的 `event`
- **THEN** 响应 `code:-3`，`msg:"fail"`
