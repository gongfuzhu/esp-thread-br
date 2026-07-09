## 1. 配置 (Kconfig.projbuild — 新建)

- [x] 1.1 新建 `deep_sleep/main/Kconfig.projbuild`,`menu "Deep Sleep Motion Sensor"`
- [x] 1.2 `config MOTION_WAKEUP_GPIO`(int,默认 8,help:运动传感器输出接入的 GPIO,H2 EXT1 合法范围 8–14)
- [x] 1.3 `config MOTION_HEARTBEAT_SEC`(int,默认 300,help:定时心跳唤醒间隔秒数)
- [x] 1.4 `config MOTION_MAX_AWAKE_MS`(int,默认 10000,help:唤醒后最大清醒兜底毫秒,到点强制回深睡)
- [x] 1.5 `config MOTION_SRP_SERVICE_NAME`(string,默认 `_iot._udp`)、`config MOTION_ACK_URI`(string,默认 `ack`)、`config MOTION_COAP_PORT`(int,默认 5683),help 注明须与 BR `mqtt_ot_bridge` 一致

## 2. EUI64 辅助 (复用)

- [x] 2.1 从 `ot_iot_device/main` 复制 `device_eui64.c` 和 `device_eui64.h` 到 `deep_sleep/main/`

## 3. 构建配置 (CMakeLists.txt)

- [x] 3.1 在 `deep_sleep/main/CMakeLists.txt` 的 SRCS 加入 `device_eui64.c`
- [x] 3.2 PRIV_REQUIRES 增补上报所需组件(esp_random 属 esp_hw_support / 已随核心可用;确认 openthread 已在),INCLUDE_DIRS 保持 "."

## 4. 唤醒源与事件判定 (esp_ot_sleepy_device.c)

- [x] 4.1 新增静态变量保存本次唤醒的事件类型字符串(`s_event`),及 EUI64 字符串缓存
- [x] 4.2 在 `ot_deep_sleep_init()` 读唤醒原因处,映射 `EXT1→"motion"` / `TIMER→"heartbeat"` / `UNDEFINED→"boot"` 写入 `s_event`
- [x] 4.3 定时唤醒间隔由 20s 改为 `CONFIG_MOTION_HEARTBEAT_SEC`
- [x] 4.4 GPIO 唤醒改为 `esp_sleep_enable_ext1_wakeup_io(1ULL<<CONFIG_MOTION_WAKEUP_GPIO, ESP_EXT1_WAKEUP_ANY_HIGH)`;移除/调整原 GPIO9 下拉+ANY_LOW 相关配置(改为适配高电平触发的引脚配置)

## 5. SRP 注册与上报 (esp_ot_sleepy_device.c)

- [x] 5.1 移植 `ot_iot_device` 的 `srp_register(otInstance*)`:EUI64 作 host/instance 名,service 名取 `CONFIG_MOTION_SRP_SERVICE_NAME`,`otSrpClientEnableAutoStartMode`
- [x] 5.2 实现 `device_report(const char *event)`:手写 JSON `{"id":<eui64>,"reqid":<esp_random 十六进制>,"event":<event>}`,构造 CoAP NON POST 到 `CONFIG_MOTION_ACK_URI`,目标为 `otSrpClientGetServerAddress` 返回地址、端口 `CONFIG_MOTION_COAP_PORT`;server 地址为 NULL 时不发
- [x] 5.3 上报的 `otCoapSendRequest` 使用响应/完成路径或发送后确认已 flush,作为进深睡的触发信号

## 6. 睡眠触发改造 (esp_ot_sleepy_device.c)

- [x] 6.1 移除"到 CHILD 后固定 5 秒即睡"的旧逻辑(`s_oneshot_timer` 那条路径)
- [x] 6.2 改为:SRP 注册取得 server 地址后调用 `device_report(s_event)`,上报发出后 `esp_deep_sleep_start()`(经 `gettimeofday` 记录进睡时间保持原有睡眠时长打印)
- [x] 6.3 新增最大清醒兜底单次定时器,周期 `CONFIG_MOTION_MAX_AWAKE_MS`,回调强制 `esp_deep_sleep_start()`;在 `ot_deep_sleep_init` 或 app_main 启动
- [x] 6.4 确保两条进睡路径(上报完成 / 兜底)不会重复进睡或竞态(`esp_deep_sleep_start()` 不返回,先触发者胜出,天然互斥;无需额外标志)

## 7. 文档

- [x] 7.1 更新 `deep_sleep/README.md`:接线说明(VCC+GND 共地+OUT→GPIO8、OUT 须 3.3V 高电平/空闲低、推挽无需外部电阻)、事件语义(motion/heartbeat/boot)、须与 BR `mqtt_ot_bridge` 配置一致

## 8. 验证

- [x] 8.1 在 ESP-IDF PowerShell/CMD 下 `idf.py set-target esp32h2 && idf.py build` 编译通过
- [x] 8.2 硬件联调:传感器触发(或手动拉高 GPIO8)→ 观察 monitor 打印 "Wake up from GPIO" 与上报日志 → 回深睡(monitor 实测:SRP found → `reported event=motion` → ~1s 后 Enter deep sleep,约 700ms 完成上报,从未触发兜底)
- [ ] 8.3 验证 MQTT `<prefix>/dev/response` 收到 `{"id":..,"reqid":..,"event":"motion"}`(待验证:需订阅 broker 确认)
- [ ] 8.4 验证心跳:等待 `MOTION_HEARTBEAT_SEC` 后自动唤醒并上报 `event:"heartbeat"`(待验证)
- [ ] 8.5 验证兜底:断开 BR/网络,唤醒后在 `MOTION_MAX_AWAKE_MS` 内回深睡,不持续清醒(待验证)
