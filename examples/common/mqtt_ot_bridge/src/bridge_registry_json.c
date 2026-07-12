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

char *bridge_registry_list_resp_to_json(const char *reqid, const char *br_eui64, const bridge_dev_entry_t *entries, size_t count) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    cJSON_AddStringToObject(root, "reqid", reqid ? reqid : "");
    cJSON_AddStringToObject(root, "eui64", br_eui64 ? br_eui64 : "");
    cJSON_AddStringToObject(root, "event", "registry_list_resp");
    cJSON_AddNumberToObject(root, "code", 0);
    cJSON_AddStringToObject(root, "msg", "success");

    cJSON *data = cJSON_CreateObject();
    if (data == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON *list = cJSON_CreateArray();
    if (list == NULL) {
        cJSON_Delete(data);
        cJSON_Delete(root);
        return NULL;
    }

    for (size_t i = 0; i < count; i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            cJSON_Delete(list);
            cJSON_Delete(data);
            cJSON_Delete(root);
            return NULL;
        }
        cJSON_AddStringToObject(item, "eui64", entries[i].eui64);
        cJSON_AddStringToObject(item, "ipv6", entries[i].ipv6);
        cJSON_AddStringToObject(item, "service", entries[i].service);
        cJSON_AddItemToArray(list, item);
    }

    cJSON_AddItemToObject(data, "list", list);
    cJSON_AddItemToObject(root, "data", data);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}
