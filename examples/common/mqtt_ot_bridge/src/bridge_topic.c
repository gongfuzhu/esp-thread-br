#include "bridge_topic.h"
#include "bridge_eui64.h"
#include <string.h>

#define UNICAST_PREFIX "unicast/"

bool bridge_topic_parse(const char *topic_suffix, bridge_cmd_t *out) {
    if (topic_suffix == NULL || out == NULL) {
        return false;
    }
    out->kind = BRIDGE_CMD_NONE;

    if (strcmp(topic_suffix, "multicast") == 0) {
        out->kind = BRIDGE_CMD_MULTICAST;
        return true;
    }
    size_t plen = strlen(UNICAST_PREFIX);
    if (strncmp(topic_suffix, UNICAST_PREFIX, plen) == 0) {
        const char *id = topic_suffix + plen;
        if (eui64_from_string(id, out->eui64)) {
            out->kind = BRIDGE_CMD_UNICAST;
            return true;
        }
    }
    return false;
}
