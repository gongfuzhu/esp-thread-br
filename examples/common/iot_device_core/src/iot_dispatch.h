#pragma once
#include <stdbool.h>
#include "iot_device_core.h"   // iot_event_handler_t

#define IOT_DISPATCH_MAX 16

// 注册 event→handler。重复或表满返回 false。event 指针须长期有效。
bool iot_dispatch_register(const char *event, iot_event_handler_t h);

// 查表。未命中返回 NULL。
iot_event_handler_t iot_dispatch_lookup(const char *event);

// 清空表(测试辅助)。
void iot_dispatch_reset(void);
