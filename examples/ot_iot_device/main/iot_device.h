#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 启动 IoT 设备逻辑：SRP 自动注册 + CoAP server(ctrl 资源)。
// 必须在 esp_openthread_start() 之后调用。
void iot_device_start(void);

#ifdef __cplusplus
}
#endif
