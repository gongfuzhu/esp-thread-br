#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "../../src/iot_envelope.h"

static void test_parse_ok(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    const char *in = "{\"reqid\":\"r1\",\"event\":\"switch\",\"data\":{\"gpio\":2,\"action\":\"on\"}}";
    assert(iot_envelope_parse(in, reqid, sizeof(reqid), event, sizeof(event), &data));
    assert(strcmp(reqid, "r1") == 0);
    assert(strcmp(event, "switch") == 0);
    assert(data != NULL);
    cJSON *gpio = cJSON_GetObjectItem(data, "gpio");
    assert(cJSON_IsNumber(gpio) && gpio->valueint == 2);
    cJSON_Delete(data);
}

static void test_parse_empty_data(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    const char *in = "{\"reqid\":\"r2\",\"event\":\"registry_list\",\"data\":{}}";
    assert(iot_envelope_parse(in, reqid, sizeof(reqid), event, sizeof(event), &data));
    assert(strcmp(event, "registry_list") == 0);
    assert(data != NULL && cJSON_IsObject(data));
    cJSON_Delete(data);
}

static void test_parse_missing_event(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    const char *in = "{\"reqid\":\"r3\",\"data\":{}}";
    assert(!iot_envelope_parse(in, reqid, sizeof(reqid), event, sizeof(event), &data));
    assert(data == NULL);
}

static void test_parse_bad_json(void) {
    char reqid[64], event[64];
    cJSON *data = NULL;
    assert(!iot_envelope_parse("not json", reqid, sizeof(reqid), event, sizeof(event), &data));
}

static void test_build_success(void) {
    cJSON *rd = cJSON_CreateObject();
    cJSON_AddNumberToObject(rd, "gpio", 2);
    cJSON_AddStringToObject(rd, "status", "on");
    char *out = iot_envelope_build_resp("r1", "1a2b3c4d5e6f7080", "switch", 0, rd);
    cJSON_Delete(rd);
    assert(out != NULL);
    cJSON *p = cJSON_Parse(out);
    assert(strcmp(cJSON_GetObjectItem(p, "reqid")->valuestring, "r1") == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "eui64")->valuestring, "1a2b3c4d5e6f7080") == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "event")->valuestring, "switch_resp") == 0);
    assert(cJSON_GetObjectItem(p, "code")->valueint == 0);
    assert(strcmp(cJSON_GetObjectItem(p, "msg")->valuestring, "success") == 0);
    assert(cJSON_GetObjectItem(cJSON_GetObjectItem(p, "data"), "gpio")->valueint == 2);
    cJSON_Delete(p);
    free(out);
}

static void test_build_fail_null_data(void) {
    char *out = iot_envelope_build_resp("r9", "aabb", "adc_read", -1, NULL);
    assert(out != NULL);
    cJSON *p = cJSON_Parse(out);
    assert(cJSON_GetObjectItem(p, "code")->valueint == -1);
    assert(strcmp(cJSON_GetObjectItem(p, "msg")->valuestring, "fail") == 0);
    assert(cJSON_IsObject(cJSON_GetObjectItem(p, "data")));  // 空对象
    cJSON_Delete(p);
    free(out);
}

int main(void) {
    test_parse_ok();
    test_parse_empty_data();
    test_parse_missing_event();
    test_parse_bad_json();
    test_build_success();
    test_build_fail_null_data();
    printf("test_envelope OK\n");
    return 0;
}