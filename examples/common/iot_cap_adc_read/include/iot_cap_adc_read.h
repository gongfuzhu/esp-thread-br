#pragma once
#ifdef __cplusplus
extern "C" {
#endif
// 注册 "adc_read" event。须在 iot_device_core_start() 之后调用。
void iot_cap_adc_read_init(void);
#ifdef __cplusplus
}
#endif