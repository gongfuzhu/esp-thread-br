#pragma once
#include <stdint.h>
#include <stdbool.h>

// 16位十六进制文本(无分隔符) -> 8字节，首字节对应 out[0]。成功返回 true。
bool eui64_from_string(const char *s, uint8_t out[8]);
// 8字节 -> 16位小写十六进制 + '\0'(out 至少 17 字节)。
void eui64_to_string(const uint8_t in[8], char out[17]);
