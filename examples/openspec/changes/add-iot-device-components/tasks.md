## 1. 内核组件 iot_device_core

- [ ] 1.1 建 `common/iot_device_core` 骨架（CMakeLists REQUIRES `openthread`、idf_component.yml 依赖 `espressif/cjson`、Kconfig.projbuild、include/src 布局）
- [ ] 1.2 实现 SRP 自动注册（EUI64 作 host，service 类型/端口来自 Kconfig）
- [ ] 1.3 实现 CoAP server：注册 `ctrl` 资源 + 订阅组播地址（默认 `ff03::1`）
- [ ] 1.4 实现命令 worker 任务 + 队列：CoAP 回调仅解析信封入队即返回，worker 取出执行；栈深经 Kconfig 配置；队列满丢弃并记日志
- [ ] 1.5 实现下行信封解析 `{reqid,event,data}` 与 `event → handler` 分发表 + `iot_device_register_handler()` 接口
- [ ] 1.6 实现响应信封封装（透传 reqid、填 eui64、拼 `_resp`、据返回码填 code/msg）与单播 NON 上报到 BR `ack`
- [ ] 1.7 组播上报 0~500ms 抖动（FreeRTOS 单次定时器承载，不在 CoAP 回调 vTaskDelay）
- [ ] 1.8 导出公共头 `iot_device_core.h`（start、register_handler 等）

## 2. 样板能力组件

- [ ] 2.1 建 `common/iot_cap_switch`：`iot_cap_switch_init()` 注册 `switch`，解析 gpio/action，驱动 GPIO，回填 status
- [ ] 2.2 iot_cap_switch 支持可选 `delay`/`hold`（FreeRTOS 定时器承载，不阻塞 OT）
- [ ] 2.3 建 `common/iot_cap_pwm_set`：`iot_cap_pwm_set_init()` 注册 `pwm_set`，LEDC 配置 freq/duty，组件内通道分配 static 状态
- [ ] 2.4 建 `common/iot_cap_adc_read`：`iot_cap_adc_read_init()` 注册 `adc_read`，ADC 采样 + 校准句柄复用，回填 raw_val/voltage

## 3. 改造示范工程 ot_iot_device

- [ ] 3.1 删除旧 `{reqid,cmd}` 解析与平铺的 SRP/CoAP/switch 代码，改依赖新组件
- [ ] 3.2 `app_main`/启动流程显式挂载：`iot_device_core_start()` + `iot_cap_switch_init()` + `iot_cap_pwm_set_init()` + `iot_cap_adc_read_init()`
- [ ] 3.3 更新 main 的 CMakeLists/idf_component.yml 引用四个新组件，更新 Kconfig 引脚默认值
- [ ] 3.4 编译验证 ot_iot_device（ESP32-H2）

## 4. 改造 BR mqtt_ot_bridge

- [ ] 4.1 上行响应主题 `dev/response` → `cmd/resp`（含 Kconfig 项）
- [ ] 4.2 新增订阅专用 topic `cmd/registry`
- [ ] 4.3 实现 `registry_list` 自应答：仅解析 `cmd/registry` payload、查 SRP 表、构造 `registry_list_resp` 发布到 `cmd/resp`，不转发 CoAP；`cmd/unicast/+`、`cmd/multicast` 仍字节透传
- [ ] 4.4 编译验证 basic_thread_border_router（含 bridge，ESP32-C6）

## 5. 联调验证

- [ ] 5.1 设备端 monitor：单播 `switch` 下发→执行→`switch_resp` 回执全链路
- [ ] 5.2 组播 `switch` 多设备下发→抖动上报，无风暴、无重复执行
- [ ] 5.3 `pwm_set`/`adc_read` 各验证一次（含参数错误 code:-1、未注册 event code:-3）
- [ ] 5.4 `registry_list`（发往 `cmd/registry`）→BR 应答 `registry_list_resp` 含 SRP 设备清单
