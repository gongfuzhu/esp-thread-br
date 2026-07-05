## 1. 配置

- [ ] 1.1 在 `main/Kconfig.projbuild` 新增 `config IOT_DEVICE_BLINK_MS`(int,默认 500,help 说明为 blink 亮持续毫秒数)

## 2. 设备端实现(iot_device.c)

- [ ] 2.1 新增全局单次定时器句柄 `s_blink_timer`,在 `iot_device_start()` 中 `xTimerCreate("iot_blink", 1, pdFALSE, NULL, blink_off_timer_cb)`
- [ ] 2.2 实现 `blink_off_timer_cb`:持 OT 锁 → `device_switch_set(false)` → 释放锁(参照 `report_timer_cb` 的持锁模式)
- [ ] 2.3 扩展 `parse_command`:新增 `is_blink_out` 出参,识别 `cmd:"blink"` 时置真并返回 ok
- [ ] 2.4 扩展 `device_report`:新增 `action` 参数,非 NULL 时在 JSON 加 `"action":<action>`,并在 blink 场景固定上报 `state:"off"`(不读 GPIO 瞬时值)
- [ ] 2.5 在 `ctrl_request_handler` 加 blink 分支:`device_switch_set(true)` 立即点亮 → `xTimerChangePeriod(s_blink_timer, pdMS_TO_TICKS(CONFIG_IOT_DEVICE_BLINK_MS), 0)` + `xTimerStart` 排定熄灭
- [ ] 2.6 blink 分支照常复用现有 report 定时器上报路径(含 `via_multicast` 抖动判断),上报时传 `action:"blink"`

## 3. 验证

- [ ] 3.1 在 ESP-IDF PowerShell/CMD 下 `idf.py build`(H2 目标)编译通过,无告警回归
- [ ] 3.2 硬件联调:服务端下发 `{"reqid":"t1","cmd":"blink"}`,观察板载 LED 亮约 500ms 后自动灭
- [ ] 3.3 验证 `dev/response` 收到 `{"id":..,"reqid":"t1","state":"off","action":"blink"}`
- [ ] 3.4 回归验证 `on`/`off`/`query` 行为不变
