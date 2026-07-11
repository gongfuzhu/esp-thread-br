#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 注册 "pwm_set" event。须在 iot_device_core_start() 之后调用。
void iot_cap_pwm_set_init(void);
#ifdef __cplusplus
}
#endif
