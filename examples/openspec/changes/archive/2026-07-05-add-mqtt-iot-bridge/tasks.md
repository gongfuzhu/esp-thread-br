# Tasks: add-mqtt-iot-bridge

## 1. 组件脚手架与依赖

- [ ] 1.1 创建 `common/mqtt_ot_bridge` 组件骨架(`CMakeLists.txt`、`include/mqtt_ot_bridge.h`、`src/mqtt_ot_bridge.c`、`Kconfig.projbuild`),参照 `common/thread_border_router` 组织方式
- [ ] 1.2 在 `basic_thread_border_router/main/idf_component.yml` 声明 `espressif/mqtt` 依赖;编译验证 `mqtt_client.h` 可用
- [ ] 1.3 在 `Kconfig.projbuild` 添加配置项:broker 地址/端口、用户名/密码、topic 前缀、清单刷新周期、组播地址(默认 `ff03::1`)、启用开关
- [ ] 1.4 定义公共头 API:`mqtt_ot_bridge_start()` / `mqtt_ot_bridge_stop()`

## 2. MQTT 客户端(capability: mqtt-bridge)

- [ ] 2.1 用 esp-mqtt 初始化客户端,配置用户名/密码,不启用 TLS
- [ ] 2.2 连接成功后订阅下行 topic:`cmd/unicast/+` 与 `cmd/multicast`
- [ ] 2.3 实现连接/断线/认证失败事件处理与自动重连日志,确保不影响 Thread/BR 功能
- [ ] 2.4 实现上行发布封装:把任意 payload 原样 publish 到 `dev/response`

## 3. CoAP 控制(capability: coap-device-control)

- [ ] 3.1 启动 OpenThread CoAP(`otCoapStart`),注册 `/ack` 资源(`otCoapAddResource`)
- [ ] 3.2 实现单播:MQTT `cmd/unicast/<eui64>` → 解析 EUI64→IPv6 → `otCoapSendRequest`(CON),payload 透传
- [ ] 3.3 实现单播响应回调:收到响应 payload → 调用上行发布封装
- [ ] 3.4 实现组播:MQTT `cmd/multicast` → 向 `ff03::1` 发送 NON 请求,发完即返回(无待办表/无超时)
- [ ] 3.5 实现 `/ack` 资源处理回调:收到设备上报 payload → 上行发布,按需回 ACK
- [ ] 3.6 目标不可解析/发送失败时记录错误并丢弃,保证不崩溃、不阻塞

## 4. 设备清单(capability: device-registry)

- [ ] 4.1 实现 SRP 表遍历:`otSrpServerGetNextHost` + `otSrpServerHostGetNextService`,提取 `{eui64, ipv6, service}`
- [ ] 4.2 实现 EUI64→IPv6 选路查询函数(供 3.2 使用)
- [ ] 4.3 实现清单序列化为 JSON 并以 retained 消息周期发布到 `dev/registry`
- [ ] 4.4 处理 SRP server 未启用场景:警告日志 + 空清单;在 README 标注前置条件

## 5. 集成与验证

- [ ] 5.1 在 `basic_thread_border_router/main/esp_ot_br.c` 的 BR 启动后调用 `mqtt_ot_bridge_start()`(受 Kconfig 开关保护)
- [ ] 5.2 确认 `CONFIG_OPENTHREAD_SRP_SERVER` 在示例 sdkconfig 中启用
- [ ] 5.3 编译验证(`idf.py build`);目标芯片按现有 `sdkconfig.defaults*`
- [ ] 5.4 设备端(H2)最小验证固件行为约定文档化:SRP 客户端注册(EUI64)、CoAP server、加入 `ff03::1`、组播响应随机抖动 0~500ms
- [ ] 5.5 设备端 monitor 联调:单播 CON 收回执、组播 NON 经 `/ack` 回流、`dev/registry` 清单正确
- [ ] 5.6 更新示例 README:MQTT 配置、topic 约定、前置条件、payload 由服务端定义
