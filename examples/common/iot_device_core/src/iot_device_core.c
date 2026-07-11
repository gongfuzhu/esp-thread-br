#include "iot_device_core.h"

void iot_device_core_start(void) {
    // 由后续任务实现
}

bool iot_device_register_handler(const char *event, iot_event_handler_t handler) {
    (void)event; (void)handler;
    return false;  // 由 Task 3 实现
}