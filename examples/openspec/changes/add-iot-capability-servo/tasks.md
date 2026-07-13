## 1. 能力组件 iot_cap_servo_set

- [x] 1.1 建 `common/iot_cap_servo_set` 骨架（CMakeLists REQUIRES `iot_device_core`、PRIV_REQUIRES `espressif__cjson` `esp_driver_ledc`；idf_component.yml 依赖 `espressif/cjson`；include/src 布局，导出 `iot_cap_servo_set.h` 声明 `iot_cap_servo_set_init()`）
- [x] 1.2 实现 `servo_set` handler：解析 `data.gpio`/`data.angle`，校验 angle 0~180、gpio/angle 为数字，非法回 code -1
- [x] 1.3 实现 angle→脉宽→占空比换算（0.5–2.5ms 固定映射，50Hz，LEDC 13-bit 分辨率）
- [x] 1.4 独占 `LEDC_TIMER_1`，首次调用锁 50Hz，之后仅更新占空比；组件私有 GPIO→channel 分配表（同 GPIO 复用、耗尽回 code -4，从高端分配避免与 pwm_set 冲突）
- [x] 1.5 成功回 code 0，`resp_data` 回填 `{"gpio","angle"}`
- [x] 1.6 `iot_cap_servo_set_init()` 注册 `servo_set` handler，注册失败记日志

## 2. 挂载到示范工程 ot_iot_device

- [x] 2.1 `esp_ot_iot_device.c` app_main 增加 `#include "iot_cap_servo_set.h"` 与 `iot_cap_servo_set_init()` 调用
- [x] 2.2 更新 `ot_iot_device/main` 的 CMakeLists.txt / idf_component.yml 引用新组件

## 3. 验证

- [ ] 3.1 编译 ot_iot_device（ESP32-H2）
- [ ] 3.2 设备端 monitor：单播 `servo_set {gpio,angle:90}` → 舵机转 90° → `servo_set_resp` code 0 回执
- [ ] 3.3 边界与异常：`angle:0`/`angle:180` 正常；`angle:200` 回 code -1
- [ ] 3.4 与 pwm_set 共存：先 `servo_set` 后 `pwm_set` 高频负载，确认舵机不失步（50Hz 保持）
