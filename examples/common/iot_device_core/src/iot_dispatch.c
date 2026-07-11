#include "iot_dispatch.h"
#include <string.h>

typedef struct {
    const char *event;
    iot_event_handler_t handler;
} dispatch_entry_t;

static dispatch_entry_t s_table[IOT_DISPATCH_MAX];
static int s_count = 0;

bool iot_dispatch_register(const char *event, iot_event_handler_t h) {
    if (event == NULL || h == NULL || s_count >= IOT_DISPATCH_MAX) {
        return false;
    }
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_table[i].event, event) == 0) {
            return false;   // 重复
        }
    }
    s_table[s_count].event = event;
    s_table[s_count].handler = h;
    s_count++;
    return true;
}

iot_event_handler_t iot_dispatch_lookup(const char *event) {
    if (event == NULL) {
        return NULL;
    }
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_table[i].event, event) == 0) {
            return s_table[i].handler;
        }
    }
    return NULL;
}

void iot_dispatch_reset(void) {
    s_count = 0;
}
