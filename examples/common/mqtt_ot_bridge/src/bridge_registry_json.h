#pragma once
#include <stddef.h>

typedef struct {
    char eui64[17];
    char ipv6[46];
    char service[64];
} bridge_dev_entry_t;

// 生成 JSON 数组字符串(调用者 free)。count 可为 0(entries 可 NULL)。失败返回 NULL。
char *bridge_registry_to_json(const bridge_dev_entry_t *entries, size_t count);
char *bridge_registry_list_resp_to_json(const char *reqid, const char *br_eui64, const bridge_dev_entry_t *entries, size_t count);
