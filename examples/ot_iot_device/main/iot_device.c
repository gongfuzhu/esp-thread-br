#include "iot_device.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include "device_eui64.h"
#include "device_switch.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/srp_client.h"
#include "openthread/srp_client_buffers.h"
#include "openthread/platform/radio.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"

#define TAG "iot_device"

#define COAP_PAYLOAD_MAX 512
#define REQID_MAX        64

static char s_eui64_str[17];

// 待上报请求(由 ctrl 回调填充，定时器回调消费)。单槽即可：命令处理是低频的。
static char s_pending_reqid[REQID_MAX];
static TimerHandle_t s_report_timer = NULL;
static TimerHandle_t s_blink_timer = NULL;

// ---------------------------------------------------------------------------
// SRP 自动注册(用 EUI64 作 host 名)
// ---------------------------------------------------------------------------

static void srp_autostart_cb(const otSockAddr *server, void *ctx) {
    (void)server; (void)ctx;
    ESP_LOGI(TAG, "SRP auto-start: server found, host=%s", s_eui64_str);
}

// 调用者必须已持 OT 锁。
static void srp_register(otInstance *inst) {
    uint8_t eui[8];
    otPlatRadioGetIeeeEui64(inst, eui);
    device_eui64_to_string(eui, s_eui64_str);

    uint16_t size = 0;
    char *host_name_buf = otSrpClientBuffersGetHostNameString(inst, &size);
    strncpy(host_name_buf, s_eui64_str, size - 1);
    host_name_buf[size - 1] = '\0';
    otSrpClientSetHostName(inst, host_name_buf);
    otSrpClientEnableAutoHostAddress(inst);

    otSrpClientBuffersServiceEntry *entry = otSrpClientBuffersAllocateService(inst);
    if (entry == NULL) {
        ESP_LOGE(TAG, "SRP buffer alloc failed");
        return;
    }
    char *inst_name = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size);
    strncpy(inst_name, s_eui64_str, size - 1);
    inst_name[size - 1] = '\0';

    char *svc_name = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
    strncpy(svc_name, CONFIG_IOT_DEVICE_SERVICE_NAME, size - 1);
    svc_name[size - 1] = '\0';

    entry->mService.mPort = CONFIG_IOT_DEVICE_COAP_PORT;

    otError err = otSrpClientAddService(inst, &entry->mService);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "SRP add service err=%d", err);
        return;
    }
    otSrpClientEnableAutoStartMode(inst, srp_autostart_cb, NULL);
    ESP_LOGI(TAG, "SRP registration queued: host/instance=%s service=%s",
             s_eui64_str, CONFIG_IOT_DEVICE_SERVICE_NAME);
}

// ---------------------------------------------------------------------------
// 状态上报:单播 NON POST 到 BR 的 ack 资源
// ---------------------------------------------------------------------------

// 构造状态 JSON 并单播上报到 BR(SRP server)的 ack 资源。
// 调用者必须已持 OT 锁。
static void device_report(const char *reqid) {
    otInstance *inst = esp_openthread_get_instance();

    const otSockAddr *server = otSrpClientGetServerAddress(inst);
    if (server == NULL) {
        ESP_LOGW(TAG, "report: no SRP server address yet");
        return;
    }

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "id", s_eui64_str);
    cJSON_AddStringToObject(root, "reqid", reqid);
    cJSON_AddStringToObject(root, "state", device_switch_get() ? "on" : "off");
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        return;
    }

    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) {
        free(json);
        return;
    }
    otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_IOT_DEVICE_ACK_URI);
    otCoapMessageSetPayloadMarker(msg);
    otMessageAppend(msg, json, (uint16_t)strlen(json));

    otMessageInfo info;
    memset(&info, 0, sizeof(info));
    info.mPeerAddr = server->mAddress;
    info.mPeerPort = CONFIG_IOT_DEVICE_COAP_PORT;

    otError err = otCoapSendRequest(inst, msg, &info, NULL, NULL);
    if (err != OT_ERROR_NONE) {
        otMessageFree(msg);
        ESP_LOGE(TAG, "report send err=%d", err);
    } else {
        ESP_LOGI(TAG, "reported state=%s reqid=%s", device_switch_get() ? "on" : "off", reqid);
    }
    free(json);
}

// 定时器回调:承载抖动后的上报。在定时器任务上下文，需自行持锁。
// 不在 CoAP 回调里直接 vTaskDelay，避免阻塞 OpenThread 任务。
static void report_timer_cb(TimerHandle_t t) {
    (void)t;
    esp_openthread_lock_acquire(portMAX_DELAY);
    device_report(s_pending_reqid);
    esp_openthread_lock_release();
}

// 闪烁定时器回调:关闭 LED
static void blink_off_timer_cb(TimerHandle_t t) {
    (void)t;
    esp_openthread_lock_acquire(portMAX_DELAY);
    device_switch_set(false);
    esp_openthread_lock_release();
    ESP_LOGI(TAG, "blink: LED off");
}

// ---------------------------------------------------------------------------
// CoAP ctrl 资源:解析命令 + 执行开关 + 触发上报
// ---------------------------------------------------------------------------

static int coap_read_payload(otMessage *msg, char *buf, int buf_size) {
    uint16_t offset = otMessageGetOffset(msg);
    uint16_t total  = otMessageGetLength(msg);
    int len = (int)total - (int)offset;
    if (len < 0) {
        len = 0;
    }
    if (len > buf_size - 1) {
        len = buf_size - 1;
    }
    int read = otMessageRead(msg, offset, buf, len);
    buf[read] = '\0';
    return read;
}

// 解析 {"reqid":"..","cmd":"on|off|query"}。识别成功返回 true。
static bool parse_command(const char *json, char *reqid_out, size_t reqid_cap,
                          bool *on_out, bool *is_query_out, bool *is_blink_out) {
    cJSON *root = cJSON_Parse(json);
    if (root == NULL) {
        return false;
    }
    bool ok = false;
    reqid_out[0] = '\0';
    *is_query_out = false;
    *on_out = false;
    *is_blink_out = false;

    cJSON *reqid = cJSON_GetObjectItem(root, "reqid");
    if (cJSON_IsString(reqid) && reqid->valuestring != NULL) {
        strncpy(reqid_out, reqid->valuestring, reqid_cap - 1);
        reqid_out[reqid_cap - 1] = '\0';
    }
    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
        if (strcmp(cmd->valuestring, "on") == 0) {
            *on_out = true; ok = true;
        } else if (strcmp(cmd->valuestring, "off") == 0) {
            *on_out = false; ok = true;
        } else if (strcmp(cmd->valuestring, "query") == 0) {
            *is_query_out = true; ok = true;
        } else if (strcmp(cmd->valuestring, "blink") == 0) {
            *is_blink_out = true; ok = true;
        }
    }
    cJSON_Delete(root);
    return ok;
}

// CoAP 请求回调，在 OpenThread 任务上下文中执行(已持锁)。
static void ctrl_request_handler(void *ctx, otMessage *msg, const otMessageInfo *info) {
    (void)ctx;
    char payload[COAP_PAYLOAD_MAX];
    coap_read_payload(msg, payload, sizeof(payload));

    char reqid[REQID_MAX];
    bool on = false, is_query = false, is_blink = false;
    if (!parse_command(payload, reqid, sizeof(reqid), &on, &is_query, &is_blink)) {
        ESP_LOGW(TAG, "ctrl: bad command payload");
        return;
    }
    if (!is_query) {
        device_switch_set(on);
        ESP_LOGI(TAG, "ctrl: set switch=%d reqid=%s", on, reqid);
    } else {
        ESP_LOGI(TAG, "ctrl: query reqid=%s", reqid);
    }

    // 组播命令(本地收包地址为多播，IPv6 多播首字节 0xff)加随机抖动，避免响应风暴。
    // 上报统一交给单次定时器承载：既实现抖动，又避免在 OT 回调里 vTaskDelay 阻塞协议栈。
    bool via_multicast = (info->mSockAddr.mFields.m8[0] == 0xff);
    uint32_t jitter_ms = via_multicast ? (esp_random() % 501) : 0;

    strncpy(s_pending_reqid, reqid, sizeof(s_pending_reqid) - 1);
    s_pending_reqid[sizeof(s_pending_reqid) - 1] = '\0';

    if (s_report_timer != NULL) {
        // 至少 1 tick，保证在定时器任务(而非 OT 任务)上下文里上报。
        TickType_t delay = jitter_ms > 0 ? pdMS_TO_TICKS(jitter_ms) : 1;
        xTimerChangePeriod(s_report_timer, delay, 0);
        xTimerStart(s_report_timer, 0);
        if (via_multicast) {
            ESP_LOGI(TAG, "multicast cmd, report jitter %u ms", (unsigned)jitter_ms);
        }
    }
}

static otCoapResource s_ctrl_resource = {
    .mUriPath = CONFIG_IOT_DEVICE_CTRL_URI,
    .mHandler = ctrl_request_handler,
    .mContext = NULL,
    .mNext = NULL,
};

// ---------------------------------------------------------------------------
// 启动
// ---------------------------------------------------------------------------

void iot_device_start(void) {
    device_switch_init();

    s_report_timer = xTimerCreate("iot_report", 1, pdFALSE, NULL, report_timer_cb);
    s_blink_timer = xTimerCreate("iot_blink", 1, pdFALSE, NULL, blink_off_timer_cb);

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();

    srp_register(inst);

    otError coap_err = otCoapStart(inst, CONFIG_IOT_DEVICE_COAP_PORT);
    if (coap_err == OT_ERROR_NONE) {
        otCoapAddResource(inst, &s_ctrl_resource);
        ESP_LOGI(TAG, "CoAP started, resource '%s' registered", CONFIG_IOT_DEVICE_CTRL_URI);
    } else {
        ESP_LOGE(TAG, "CoAP start err=%d", coap_err);
    }

    otIp6Address maddr;
    if (otIp6AddressFromString(CONFIG_IOT_DEVICE_MULTICAST_ADDR, &maddr) == OT_ERROR_NONE) {
        otError merr = otIp6SubscribeMulticastAddress(inst, &maddr);
        ESP_LOGI(TAG, "subscribe multicast %s err=%d", CONFIG_IOT_DEVICE_MULTICAST_ADDR, merr);
    } else {
        ESP_LOGE(TAG, "bad multicast addr");
    }

    esp_openthread_lock_release();
}
