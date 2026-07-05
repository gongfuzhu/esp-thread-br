#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BRIDGE_CMD_NONE = 0,
    BRIDGE_CMD_UNICAST,
    BRIDGE_CMD_MULTICAST,
} bridge_cmd_kind_t;

typedef struct {
    bridge_cmd_kind_t kind;
    uint8_t eui64[8];   // 仅 UNICAST 有效
} bridge_cmd_t;

// 解析去掉可配置前缀后的 topic 尾部：期望 "unicast/<eui64>" 或 "multicast"。
bool bridge_topic_parse(const char *topic_suffix, bridge_cmd_t *out);
