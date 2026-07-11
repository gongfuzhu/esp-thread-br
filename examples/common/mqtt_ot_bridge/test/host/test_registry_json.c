#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"
#include "../../src/bridge_registry_json.h"

static void test_registry_list_resp(void) {
    bridge_dev_entry_t e[2] = {
        { "1a2b3c4d5e6f7080", "fd00::1", "light01" },
        { "aabbccddeeff0011", "fd00::2", "" },
    };
    const char *reqid = "test-req-123";
    const char *br_eui64 = "0011223344556677";
    char *js = bridge_registry_list_resp_to_json(reqid, br_eui64, e, 2);
    assert(js != NULL);

    cJSON *root = cJSON_Parse(js);
    assert(cJSON_IsObject(root));
    assert(strcmp(cJSON_GetObjectItem(root, "reqid")->valuestring, reqid) == 0);
    assert(strcmp(cJSON_GetObjectItem(root, "eui64")->valuestring, br_eui64) == 0);
    assert(strcmp(cJSON_GetObjectItem(root, "event")->valuestring, "registry_list_resp") == 0);
    assert(cJSON_GetObjectItem(root, "code")->valueint == 0);
    assert(strcmp(cJSON_GetObjectItem(root, "msg")->valuestring, "success") == 0);

    cJSON *data = cJSON_GetObjectItem(root, "data");
    assert(cJSON_IsObject(data));
    cJSON *list = cJSON_GetObjectItem(data, "list");
    assert(cJSON_IsArray(list));
    assert(cJSON_GetArraySize(list) == 2);

    // Check first item
    cJSON *first = cJSON_GetArrayItem(list, 0);
    assert(strcmp(cJSON_GetObjectItem(first, "eui64")->valuestring, e[0].eui64) == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "ipv6")->valuestring, e[0].ipv6) == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "service")->valuestring, e[0].service) == 0);

    // Empty list test with NULL reqid/eui64
    char *empty_js = bridge_registry_list_resp_to_json(NULL, NULL, NULL, 0);
    assert(empty_js != NULL);
    cJSON *empty_root = cJSON_Parse(empty_js);
    assert(cJSON_IsObject(empty_root));
    // Check empty reqid and eui64 when NULL is passed
    assert(strcmp(cJSON_GetObjectItem(empty_root, "reqid")->valuestring, "") == 0);
    assert(strcmp(cJSON_GetObjectItem(empty_root, "eui64")->valuestring, "") == 0);
    cJSON *empty_data = cJSON_GetObjectItem(empty_root, "data");
    cJSON *empty_list = cJSON_GetObjectItem(empty_data, "list");
    assert(cJSON_GetArraySize(empty_list) == 0);

    cJSON_Delete(root);
    cJSON_Delete(empty_root);
    free(js);
    free(empty_js);
}

int main(void) {
    bridge_dev_entry_t e[2] = {
        { "1a2b3c4d5e6f7080", "fd00::1", "light01" },
        { "aabbccddeeff0011", "fd00::2", "" },
    };
    char *js = bridge_registry_to_json(e, 2);
    assert(js != NULL);

    cJSON *root = cJSON_Parse(js);
    assert(cJSON_IsArray(root));
    assert(cJSON_GetArraySize(root) == 2);

    cJSON *first = cJSON_GetArrayItem(root, 0);
    assert(strcmp(cJSON_GetObjectItem(first, "eui64")->valuestring, "1a2b3c4d5e6f7080") == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "ipv6")->valuestring, "fd00::1") == 0);
    assert(strcmp(cJSON_GetObjectItem(first, "service")->valuestring, "light01") == 0);

    // 空数组
    char *empty = bridge_registry_to_json(NULL, 0);
    assert(empty != NULL);
    cJSON *er = cJSON_Parse(empty);
    assert(cJSON_IsArray(er) && cJSON_GetArraySize(er) == 0);

    cJSON_Delete(root); cJSON_Delete(er);
    free(js); free(empty);
    test_registry_list_resp();
    printf("test_registry_json OK\n");
    return 0;
}
