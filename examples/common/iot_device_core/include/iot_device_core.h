#pragma once
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

// 全局状态码(协议规范 3.3)
#define IOT_CODE_OK          0    // 成功
#define IOT_CODE_PARAM      -1    // 参数错误
#define IOT_CODE_BUSY       -2    // 忙/执行失败
#define IOT_CODE_UNSUPPORTED -3   // 不支持该事件
#define IOT_CODE_HW         -4    // 硬件异常

// 能力 handler：读 data、干活、填 resp_data、返状态码。
// 运行于内核 worker 任务上下文，允许阻塞；访问 OT API 时须自行持 OT 锁。
typedef int (*iot_event_handler_t)(const cJSON *data, cJSON *resp_data);

// 启动内核：SRP 自动注册 + CoAP server + 命令 worker 任务。
// 必须在 esp_openthread_start() 之后调用。
void iot_device_core_start(void);

// 注册一个 event 的 handler。event 字符串须在程序生命周期内保持有效
// (通常是字面量)。返回 true 成功，表满或重复返回 false。
bool iot_device_register_handler(const char *event, iot_event_handler_t handler);

#ifdef __cplusplus
}
#endif