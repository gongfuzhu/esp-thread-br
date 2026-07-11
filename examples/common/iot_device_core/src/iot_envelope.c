#include "iot_envelope.h"
#include <string.h>
#include <stdio.h>

static void copy_str(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    if (src == NULL) { dst[0] = '\0'; return; }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

bool iot_envelope_parse(const char *json,
                        char *reqid, size_t reqid_cap,
                        char *event, size_t event_cap,
                        cJSON **data_out) {
    if (data_out) *data_out = NULL;
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    cJSON *jevent = cJSON_GetObjectItem(root, "event");
    if (!cJSON_IsString(jevent) || jevent->valuestring == NULL) {
        cJSON_Delete(root);
        return false;
    }
    copy_str(event, event_cap, jevent->valuestring);

    cJSON *jreqid = cJSON_GetObjectItem(root, "reqid");
    copy_str(reqid, reqid_cap, cJSON_IsString(jreqid) ? jreqid->valuestring : "");

    if (data_out) {
        cJSON *jdata = cJSON_GetObjectItem(root, "data");
        if (jdata != NULL) {
            *data_out = cJSON_Duplicate(jdata, true);
        } else {
            *data_out = cJSON_CreateObject();
        }
    }
    cJSON_Delete(root);
    return true;
}

char *iot_envelope_build_resp(const char *reqid, const char *eui64,
                              const char *event, int code,
                              const cJSON *resp_data) {
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "reqid", reqid ? reqid : "");
    cJSON_AddStringToObject(root, "eui64", eui64 ? eui64 : "");

    char ev[80];
    snprintf(ev, sizeof(ev), "%s_resp", event ? event : "");
    cJSON_AddStringToObject(root, "event", ev);

    cJSON_AddNumberToObject(root, "code", code);
    cJSON_AddStringToObject(root, "msg", code == 0 ? "success" : "fail");

    cJSON *data = resp_data ? cJSON_Duplicate(resp_data, true) : cJSON_CreateObject();
    cJSON_AddItemToObject(root, "data", data);

    char *out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return out;
}