#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "cJSON.h"
#include "../../src/bridge_registry_json.h"

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
    printf("test_registry_json OK\n");
    return 0;
}
