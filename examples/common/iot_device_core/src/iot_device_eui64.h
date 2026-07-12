#pragma once
#include <stdint.h>
// 8 字节 EUI64 -> 16 位小写十六进制 + '\0'(out 至少 17 字节)。
void iot_eui64_to_string(const uint8_t in[8], char out[17]);
