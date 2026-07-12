#include "iot_device_core.h"
#include "iot_dispatch.h"
#include "iot_envelope.h"
#include "iot_device_eui64.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_random.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/instance.h"
#include "openthread/srp_client.h"
#include "openthread/srp_client_buffers.h"
#include "openthread/platform/radio.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"

#define TAG "iot_device_core"
#define REQID_MAX        64
#define EVENT_MAX        64
#define COAP_PAYLOAD_MAX 512

static char s_eui64_str[17];
static QueueHandle_t s_cmd_queue = NULL;

// 投递到 worker 的命令。data 为堆上 JSON 字符串(worker 负责 free)。
typedef struct {
    char reqid[REQID_MAX];
    char event[EVENT_MAX];
    char *data_json;     // {reqid,event,data} 原始 payload 的副本
    bool via_multicast;
} cmd_msg_t;

// 公共 API：注册转发到 dispatch 表
bool iot_device_register_handler(const char *event, iot_event_handler_t handler) {
    return iot_dispatch_register(event, handler);
}

// ---- SRP 自动注册(EUI64 作 host) ----
static void srp_autostart_cb(const otSockAddr *server, void *ctx) {
    (void)server; (void)ctx;
    ESP_LOGI(TAG, "SRP auto-start: host=%s", s_eui64_str);
}

// 调用者须已持 OT 锁。
static void srp_register(otInstance *inst) {
    uint8_t eui[8];
    otPlatRadioGetIeeeEui64(inst, eui);
    iot_eui64_to_string(eui, s_eui64_str);

    uint16_t size = 0;
    char *host = otSrpClientBuffersGetHostNameString(inst, &size);
    strncpy(host, s_eui64_str, size - 1); host[size - 1] = '\0';
    otSrpClientSetHostName(inst, host);
    otSrpClientEnableAutoHostAddress(inst);

    otSrpClientBuffersServiceEntry *entry = otSrpClientBuffersAllocateService(inst);
    if (entry == NULL) { ESP_LOGE(TAG, "SRP alloc failed"); return; }
    char *iname = otSrpClientBuffersGetServiceEntryInstanceNameString(entry, &size);
    strncpy(iname, s_eui64_str, size - 1); iname[size - 1] = '\0';
    char *sname = otSrpClientBuffersGetServiceEntryServiceNameString(entry, &size);
    strncpy(sname, CONFIG_IOT_CORE_SERVICE_NAME, size - 1); sname[size - 1] = '\0';
    entry->mService.mPort = CONFIG_IOT_CORE_COAP_PORT;

    if (otSrpClientAddService(inst, &entry->mService) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "SRP add service failed"); return;
    }
    otSrpClientEnableAutoStartMode(inst, srp_autostart_cb, NULL);
    ESP_LOGI(TAG, "SRP queued host/instance=%s service=%s", s_eui64_str, CONFIG_IOT_CORE_SERVICE_NAME);
}

// ---- 上报：单播 NON POST 到 BR ack 资源。调用者须已持 OT 锁。 ----
static void report_response(const char *json) {
    otInstance *inst = esp_openthread_get_instance();
    const otSockAddr *server = otSrpClientGetServerAddress(inst);
    if (server == NULL) { ESP_LOGW(TAG, "report: no SRP server yet"); return; }

    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) return;
    otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_IOT_CORE_ACK_URI);
    otCoapMessageSetPayloadMarker(msg);
    otMessageAppend(msg, json, (uint16_t)strlen(json));

    otMessageInfo info; memset(&info, 0, sizeof(info));
    info.mPeerAddr = server->mAddress;
    info.mPeerPort = CONFIG_IOT_CORE_COAP_PORT;
    if (otCoapSendRequest(inst, msg, &info, NULL, NULL) != OT_ERROR_NONE) {
        otMessageFree(msg);
        ESP_LOGE(TAG, "report send failed");
    }
}

// ---- 命令 worker 任务 ----
static void process_cmd(const cmd_msg_t *m) {
    // 组播加 0~500ms 抖动，避免响应风暴
    if (m->via_multicast) {
        vTaskDelay(pdMS_TO_TICKS(esp_random() % 501));
    }
    // 解析 data
    char reqid[REQID_MAX], event[EVENT_MAX];
    cJSON *data = NULL;
    int code;
    cJSON *resp_data = cJSON_CreateObject();
    if (!iot_envelope_parse(m->data_json, reqid, sizeof(reqid), event, sizeof(event), &data)) {
        ESP_LOGW(TAG, "worker: bad envelope");
        cJSON_Delete(resp_data);
        return;   // 无法解析则无法回执(缺 reqid/event)
    }
    iot_event_handler_t h = iot_dispatch_lookup(event);
    if (h == NULL) {
        code = IOT_CODE_UNSUPPORTED;
    } else {
        code = h(data, resp_data);   // 可阻塞；handler 内部若需 OT API 自行持锁
    }
    char *out = iot_envelope_build_resp(reqid, s_eui64_str, event, code, resp_data);
    cJSON_Delete(data);
    cJSON_Delete(resp_data);
    if (out != NULL) {
        esp_openthread_lock_acquire(portMAX_DELAY);
        report_response(out);
        esp_openthread_lock_release();
        free(out);
    }
}

static void worker_task(void *arg) {
    (void)arg;
    cmd_msg_t m;
    for (;;) {
        if (xQueueReceive(s_cmd_queue, &m, portMAX_DELAY) == pdTRUE) {
            process_cmd(&m);
            free(m.data_json);
        }
    }
}

// ---- CoAP ctrl 资源：仅入队，绝不阻塞 OT 任务 ----
static int coap_read_payload(otMessage *msg, char *buf, int cap) {
    uint16_t off = otMessageGetOffset(msg);
    uint16_t tot = otMessageGetLength(msg);
    int len = (int)tot - (int)off;
    if (len < 0) len = 0;
    if (len > cap - 1) len = cap - 1;
    int rd = otMessageRead(msg, off, buf, len);
    buf[rd] = '\0';
    return rd;
}

static void ctrl_handler(void *ctx, otMessage *msg, const otMessageInfo *info) {
    (void)ctx;
    char payload[COAP_PAYLOAD_MAX];
    int len = coap_read_payload(msg, payload, sizeof(payload));
    if (len <= 0 || s_cmd_queue == NULL) return;

    cmd_msg_t m;
    memset(&m, 0, sizeof(m));
    m.data_json = malloc(len + 1);
    if (m.data_json == NULL) { ESP_LOGE(TAG, "ctrl: oom"); return; }
    memcpy(m.data_json, payload, len);
    m.data_json[len] = '\0';
    m.via_multicast = (info->mSockAddr.mFields.m8[0] == 0xff);

    if (xQueueSend(s_cmd_queue, &m, 0) != pdTRUE) {
        ESP_LOGW(TAG, "cmd queue full, drop");
        free(m.data_json);
    }
}

static otCoapResource s_ctrl_resource = {
    .mUriPath = CONFIG_IOT_CORE_CTRL_URI,
    .mHandler = ctrl_handler,
    .mContext = NULL,
    .mNext = NULL,
};

void iot_device_core_start(void) {
    s_cmd_queue = xQueueCreate(CONFIG_IOT_CORE_WORKER_QUEUE_LEN, sizeof(cmd_msg_t));
    if (s_cmd_queue == NULL) { ESP_LOGE(TAG, "queue create failed"); return; }
    xTaskCreate(worker_task, "iot_worker", CONFIG_IOT_CORE_WORKER_STACK, NULL, 5, NULL);

    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();
    srp_register(inst);
    if (otCoapStart(inst, CONFIG_IOT_CORE_COAP_PORT) == OT_ERROR_NONE) {
        otCoapAddResource(inst, &s_ctrl_resource);
        ESP_LOGI(TAG, "CoAP started, resource '%s'", CONFIG_IOT_CORE_CTRL_URI);
    } else {
        ESP_LOGE(TAG, "CoAP start failed");
    }
    otIp6Address maddr;
    if (otIp6AddressFromString(CONFIG_IOT_CORE_MULTICAST_ADDR, &maddr) == OT_ERROR_NONE) {
        otIp6SubscribeMulticastAddress(inst, &maddr);
        ESP_LOGI(TAG, "subscribed multicast %s", CONFIG_IOT_CORE_MULTICAST_ADDR);
    }
    esp_openthread_lock_release();
}
