## Why

`物联网IoT指令响应协议规范` §8.1 定义了 `servo_set`（`data:{gpio,angle}` → `servo_set_resp{gpio,angle}`）舵机角度控制事件，但设备端固件从未实现：`ot_iot_device` 只挂载了 `switch`/`pwm_set`/`adc_read` 三个能力，分发表里没有 `servo_set` 的 handler。因此云端下发 `servo_set` 时，内核 `iot_dispatch_lookup` 未命中，如实回 `code:-3`（不支持该事件）。

现有 `iot_cap_pwm_set` 虽能间接驱动舵机（舵机本质是 50Hz PWM），但存在两处硬伤：`duty` 为整数百分比、分辨率约 1.8°/步、角度粗糙；且所有通道共用单一 `LEDC_TIMER_0`，若同时驱动其它高频负载会互相覆盖 freq，舵机失步。用 `pwm_set` 属绕路，且未落地 §8.1 的 `{gpio,angle}` 契约。

本次新增专用能力组件 `iot_cap_servo_set`，把 angle→脉宽换算封进固件，让 §8.1 真正落地。

## What Changes

- 新建 `common/iot_cap_servo_set` 能力组件（一 event 一组件，显式 `iot_cap_servo_set_init()` 注册）：注册 `servo_set` event，解析 `data.gpio`、`data.angle`（0~180 度），按 **0.5–2.5ms 固定映射**（0°=0.5ms、90°=1.5ms、180°=2.5ms）换算为 50Hz PWM 占空比，经 LEDC 驱动输出，`resp_data` 回填 `{gpio,angle}`。
- 组件 **独占一个 LEDC timer**（区别于 `iot_cap_pwm_set` 的 `LEDC_TIMER_0`），锁定 50Hz，与 `pwm_set` 的高频负载互不干扰。
- 改造 `ot_iot_device` 示范工程：`app_main` 增加 `iot_cap_servo_set_init()` 挂载，main 的 CMakeLists/idf_component.yml 引用新组件。

## Capabilities

### New Capabilities
- `iot-capability-servo`: `servo_set` event 能力组件（舵机角度控制，angle→50Hz PWM 脉宽，独占 LEDC timer）。

## Impact

- 新增组件目录：`common/iot_cap_servo_set`（include/src/CMakeLists.txt/idf_component.yml）。
- 改造工程：`ot_iot_device/main`（app_main 挂载、CMakeLists/idf_component.yml 引用）。
- 依赖：组件依赖 `iot_device_core`、`espressif/cjson`、IDF `esp_driver_ledc`。
- 云端/服务端：`servo_set` event 由设备端应答 `servo_set_resp`（此前恒回 code:-3），协议 §8.1 生效，需同步联调。
- 无主机端测试套件，验证靠编译 + 设备端 monitor（ESP32-H2 设备）。
