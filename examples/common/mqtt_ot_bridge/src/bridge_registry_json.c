#include "bridge_registry_json.h"
#include "cJSON.h"

char *bridge_registry_to_json(const bridge_dev_entry_t *entries, size_t count) {
    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        cJSON *o = cJSON_CreateObject();
        if (o == NULL) {
            cJSON_Delete(arr);
            return NULL;
        }
        cJSON_AddStringToObject(o, "eui64", entries[i].eui64);
        cJSON_AddStringToObject(o, "ipv6", entries[i].ipv6);
        cJSON_AddStringToObject(o, "service", entries[i].service);
        cJSON_AddItemToArray(arr, o);
    }
    char *out = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    return out;
}
