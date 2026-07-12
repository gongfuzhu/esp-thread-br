#pragma once
#include <stddef.h>
#include <stdbool.h>
#include "cJSON.h"

// 解析下行 {reqid,event,data}。event 必填(缺失/非字符串→false)。
// reqid 缺失→""; data 缺失→空对象。成功时 *data_out 为独立深拷贝，调用者负责 cJSON_Delete。
bool iot_envelope_parse(const char *json,
                        char *reqid, size_t reqid_cap,
                        char *event, size_t event_cap,
                        cJSON **data_out);

// 构造上行响应 {reqid,eui64,event:"<event>_resp",code,msg,data}。
// msg = code==0?"success":"fail"; data = resp_data 深拷贝或空对象。
// 返回 malloc 字符串(调用者 free)或 NULL。
char *iot_envelope_build_resp(const char *reqid, const char *eui64,
                              const char *event, int code,
                              const cJSON *resp_data);