#pragma once
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// 启动 MQTT↔CoAP 桥接：连接 broker、启动 CoAP、注册 /ack、启动清单定时器。
// 必须在 OpenThread/BR 已启动之后调用。
esp_err_t mqtt_ot_bridge_start(void);

// 停止桥接(释放 MQTT 客户端与定时器)。
void mqtt_ot_bridge_stop(void);

#ifdef __cplusplus
}
#endif
