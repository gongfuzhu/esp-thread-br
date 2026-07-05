#pragma once
#include <stdbool.h>

// 初始化开关 GPIO(输出，初始关)。
void device_switch_init(void);
// 设置开关状态并驱动 GPIO。
void device_switch_set(bool on);
// 读取当前开关状态。
bool device_switch_get(void);
