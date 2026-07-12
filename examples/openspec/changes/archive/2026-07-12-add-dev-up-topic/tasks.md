## 1. BR: 上行分流

- [x] 1.1 将 `publish_uplink(payload,len)` 重构为 `publish_uplink_to(const char *suffix, const char *payload, int len)`,topic 由 `<prefix>/<suffix>` 拼成;保留 `publish_uplink` 语义调用点或改为传 `"cmd/resp"`
- [x] 1.2 `ack_request_handler` 改为发布到 `cmd/resp`(经 `publish_uplink_to`);单播 CON 回调 `unicast_response_handler` 同样发 `cmd/resp`
- [x] 1.3 新增 `devup_request_handler`,读 payload 后 `publish_uplink_to("dev/up", ...)`
- [x] 1.4 新增 static `s_devup_resource`(`.mUriPath = CONFIG_MQTT_OT_BRIDGE_DEVUP_URI`,handler 为 `devup_request_handler`)
- [x] 1.5 在 `coap_ensure_started` 内,`otCoapAddResource` 注册 `s_devup_resource`(与 `s_ack_resource` 同一 OT 锁作用域)

## 2. BR: 配置

- [x] 2.1 `common/mqtt_ot_bridge/Kconfig.projbuild` 新增 `MQTT_OT_BRIDGE_DEVUP_URI`(string,default `"devup"`),help 注明"设备主动上报的 CoAP 资源 URI,发布到 <prefix>/dev/up"

## 3. deep_sleep: 信封与 URI

- [x] 3.1 `esp_ot_sleepy_device.c` 的 `device_report` snprintf 改为 `{"reqid":"%08x","eui64":"%s","event":"%s","data":{}}`(字段 id→eui64,补空 data;注意参数顺序)
- [x] 3.2 `deep_sleep/main/Kconfig.projbuild` 的 `MOTION_ACK_URI` 默认由 `"ack"` 改 `"devup"`,help 注明"须与 BR MQTT_OT_BRIDGE_DEVUP_URI 一致"

## 4. 文档

- [x] 4.1 `common/mqtt_ot_bridge/docs/MQTT_API.md` 新增 `dev/up` 上行通道段,说明主动上报信封 `{reqid,eui64,event,data}` 与 `cmd/resp`(命令应答)的语义区分
- [x] 4.2 更新 `common/mqtt_ot_bridge/README.md` 上行 topic 表,补 `dev/up` 行与配对升级注意事项

## 5. 验证(设备端)

- [x] 5.1 编译 `basic_thread_border_router`(或含 mqtt_ot_bridge 的 BR 示例)与 `deep_sleep`,均通过
- [x] 5.2 硬件:motion/heartbeat 触发,BR monitor 显示 `devup` 资源收包,MQTT `otbr/dev/up` 收到新信封 payload,且**不再**出现在 `cmd/resp`
- [x] 5.3 硬件:IoT 组件命令应答仍正常出现在 `cmd/resp`,未受影响
