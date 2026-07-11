#include "mqtt_ot_bridge.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "bridge_topic.h"
#include "bridge_eui64.h"
#include "bridge_registry_json.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"
#include "openthread/srp_server.h"
#include "openthread/platform/radio.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#define TAG "mqtt_ot_bridge"

// TLS CA 证书（由组件 CMakeLists.txt 的 EMBED_TXTFILES 嵌入）
extern const uint8_t mqtt_ca_cert_pem_start[] asm("_binary_mqtt_ca_cert_pem_start");
extern const uint8_t mqtt_ca_cert_pem_end[]   asm("_binary_mqtt_ca_cert_pem_end");

#define REGISTRY_MAX_DEVICES 32
#define COAP_PAYLOAD_MAX     512

static esp_mqtt_client_handle_t s_client = NULL;
static bool s_coap_started = false;
static TimerHandle_t s_registry_timer = NULL;

static void handle_downlink(const char *topic_suffix, const char *data, int data_len);

// ---------------------------------------------------------------------------
// 通用工具
// ---------------------------------------------------------------------------

static const char *topic_suffix_after_prefix(const char *topic) {
    // topic 形如 "<prefix>/cmd/unicast/<eui64>"，返回 "unicast/..."/"multicast"
    const char *cmd = strstr(topic, "/cmd/");
    if (cmd == NULL) {
        return NULL;
    }
    return cmd + strlen("/cmd/");
}

// BR 自身 EUI64(小写十六进制)。懒初始化，须已持 OT 锁调用。
static const char *br_eui64_str(void) {
    static char s[17] = {0};
    if (s[0] == '\0') {
        uint8_t eui[8];
        otPlatRadioGetIeeeEui64(esp_openthread_get_instance(), eui);
        static const char *hexd = "0123456789abcdef";
        for (int i = 0; i < 8; i++) {
            s[i*2]   = hexd[(eui[i] >> 4) & 0xf];
            s[i*2+1] = hexd[eui[i] & 0xf];
        }
        s[16] = '\0';
    }
    return s;
}

static void publish_uplink(const char *payload, int len) {
    if (s_client == NULL) {
        return;
    }
    char t[128];
    snprintf(t, sizeof(t), "%s/cmd/resp", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_publish(s_client, t, payload, len, 0, 0);
}

// 从 CoAP message 读取 payload 到 buf(不含 CoAP 头)。返回读取字节数。
static int coap_read_payload(otMessage *msg, char *buf, int buf_size) {
    uint16_t offset = otMessageGetOffset(msg);
    uint16_t total  = otMessageGetLength(msg);
    int payload_len = (int)total - (int)offset;
    if (payload_len < 0) {
        payload_len = 0;
    }
    if (payload_len > buf_size - 1) {
        payload_len = buf_size - 1;
    }
    int read = otMessageRead(msg, offset, buf, payload_len);
    buf[read] = '\0';
    return read;
}

// ---------------------------------------------------------------------------
// SRP 设备发现 / EUI64 -> IPv6 选路(Task 8)
// ---------------------------------------------------------------------------

// 从 SRP host full name 前缀提取 EUI64 文本(取前16个十六进制字符)。成功写入 out[17]。
static bool host_name_to_eui64_str(const char *host_name, char out[17]) {
    if (host_name == NULL) {
        return false;
    }
    for (int i = 0; i < 16; i++) {
        char c = host_name[i];
        bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
        out[i] = (c >= 'A' && c <= 'F') ? (char)(c - 'A' + 'a') : c;
    }
    out[16] = '\0';
    return true;
}

// 遍历 SRP 表收集设备。调用者必须已持 OT 锁。
static void registry_collect(bridge_dev_entry_t *arr, size_t cap, size_t *out_count) {
    otInstance *inst = esp_openthread_get_instance();
    size_t n = 0;
    const otSrpServerHost *host = NULL;
    while ((host = otSrpServerGetNextHost(inst, host)) != NULL && n < cap) {
        if (otSrpServerHostIsDeleted(host)) {
            continue;
        }
        const char *hname = otSrpServerHostGetFullName(host);
        char eui[17];
        if (!host_name_to_eui64_str(hname, eui)) {
            continue;   // 不符合 EUI64 命名约定的 host 跳过
        }
        uint8_t naddr = 0;
        const otIp6Address *addrs = otSrpServerHostGetAddresses(host, &naddr);
        bridge_dev_entry_t *e = &arr[n];
        memcpy(e->eui64, eui, sizeof(eui));
        e->ipv6[0] = '\0';
        if (naddr > 0) {
            otIp6AddressToString(&addrs[0], e->ipv6, sizeof(e->ipv6));
        }
        e->service[0] = '\0';
        const otSrpServerService *svc = otSrpServerHostGetNextService(host, NULL);
        if (svc != NULL) {
            // 上报服务类型名(如 "_iot._udp")用于区分设备种类；
            // instance name 等于 EUI64，已在 e->eui64 中，不重复上报。
            const char *sname = otSrpServerServiceGetServiceName(svc);
            if (sname != NULL) {
                strncpy(e->service, sname, sizeof(e->service) - 1);
                e->service[sizeof(e->service) - 1] = '\0';
            }
        }
        n++;
    }
    *out_count = n;
}

// 通过 SRP 表把 EUI64 解析为可路由 IPv6。调用者必须已持 OT 锁。
static bool bridge_lookup_ipv6_by_eui64(const uint8_t eui64[8], otIp6Address *out) {
    char want[17];
    eui64_to_string(eui64, want);
    otInstance *inst = esp_openthread_get_instance();
    const otSrpServerHost *host = NULL;
    while ((host = otSrpServerGetNextHost(inst, host)) != NULL) {
        if (otSrpServerHostIsDeleted(host)) {
            continue;
        }
        char eui[17];
        if (!host_name_to_eui64_str(otSrpServerHostGetFullName(host), eui)) {
            continue;
        }
        if (strcmp(eui, want) == 0) {
            uint8_t naddr = 0;
            const otIp6Address *addrs = otSrpServerHostGetAddresses(host, &naddr);
            if (naddr > 0) {
                *out = addrs[0];
                return true;
            }
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// CoAP 上行 /ack 资源 + 启动(Task 6)
// ---------------------------------------------------------------------------

static void ack_request_handler(void *ctx, otMessage *msg, const otMessageInfo *info) {
    (void)ctx; (void)info;
    char payload[COAP_PAYLOAD_MAX];
    int len = coap_read_payload(msg, payload, sizeof(payload));
    ESP_LOGI(TAG, "/%s got %d bytes -> uplink", CONFIG_MQTT_OT_BRIDGE_ACK_URI, len);
    publish_uplink(payload, len);
    // 设备用 NON 上报时无需 ACK;若为 CON，从简不显式回 ACK。
}

static otCoapResource s_ack_resource = {
    .mUriPath = CONFIG_MQTT_OT_BRIDGE_ACK_URI,
    .mHandler = ack_request_handler,
    .mContext = NULL,
    .mNext = NULL,
};

static esp_err_t coap_ensure_started(void) {
    if (s_coap_started) {
        return ESP_OK;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();
    otError err = OT_ERROR_INVALID_STATE;
    if (inst != NULL) {
        err = otCoapStart(inst, CONFIG_MQTT_OT_BRIDGE_COAP_PORT);
        if (err == OT_ERROR_NONE) {
            otCoapAddResource(inst, &s_ack_resource);
            s_coap_started = true;
        }
    }
    esp_openthread_lock_release();
    ESP_LOGI(TAG, "coap start err=%d", err);
    return err == OT_ERROR_NONE ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// CoAP 下行:单播 CON / 组播 NON(Task 7)
// ---------------------------------------------------------------------------

static void unicast_response_handler(void *ctx, otMessage *msg, const otMessageInfo *info, otError result) {
    (void)ctx; (void)info;
    if (result != OT_ERROR_NONE || msg == NULL) {
        ESP_LOGW(TAG, "unicast no response: err=%d", result);
        return;
    }
    char payload[COAP_PAYLOAD_MAX];
    int len = coap_read_payload(msg, payload, sizeof(payload));
    publish_uplink(payload, len);
}

// confirmable=true → 单播 CON(带响应回调);false → 组播 NON(无回调)。
// 调用者必须已持有 OT 锁。
static void coap_send(const otIp6Address *dst, bool confirmable, const char *payload, int len) {
    otInstance *inst = esp_openthread_get_instance();
    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) {
        ESP_LOGE(TAG, "coap new message failed");
        return;
    }
    otCoapMessageInit(msg, confirmable ? OT_COAP_TYPE_CONFIRMABLE : OT_COAP_TYPE_NON_CONFIRMABLE,
                      OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_MQTT_OT_BRIDGE_COAP_URI);
    if (len > 0) {
        otCoapMessageSetPayloadMarker(msg);
        otMessageAppend(msg, payload, (uint16_t)len);
    }
    otMessageInfo info;
    memset(&info, 0, sizeof(info));
    info.mPeerAddr = *dst;
    info.mPeerPort = CONFIG_MQTT_OT_BRIDGE_COAP_PORT;

    otError err;
    if (confirmable) {
        err = otCoapSendRequest(inst, msg, &info, unicast_response_handler, NULL);
    } else {
        err = otCoapSendRequest(inst, msg, &info, NULL, NULL);
    }
    if (err != OT_ERROR_NONE) {
        otMessageFree(msg);   // 发送失败需释放
        ESP_LOGE(TAG, "coap send err=%d", err);
    }
}

static void handle_downlink(const char *topic_suffix, const char *data, int data_len) {
    bridge_cmd_t cmd;
    if (!bridge_topic_parse(topic_suffix, &cmd)) {
        ESP_LOGW(TAG, "unknown downlink topic suffix: %s", topic_suffix);
        return;
    }
    esp_openthread_lock_acquire(portMAX_DELAY);
    if (cmd.kind == BRIDGE_CMD_UNICAST) {
        otIp6Address dst;
        if (bridge_lookup_ipv6_by_eui64(cmd.eui64, &dst)) {
            // 单播也用 NON：回执统一走设备 → /ack → dev/response，由服务端凭 reqid 对账。
            // 若用 CON 而设备不回 CoAP ACK，会触发 BR 重传，导致设备重复执行与重复上报。
            coap_send(&dst, false, data, data_len);
        } else {
            ESP_LOGW(TAG, "unicast target not found in SRP");
        }
    } else if (cmd.kind == BRIDGE_CMD_MULTICAST) {
        otIp6Address maddr;
        if (otIp6AddressFromString(CONFIG_MQTT_OT_BRIDGE_MULTICAST_ADDR, &maddr) == OT_ERROR_NONE) {
            coap_send(&maddr, false, data, data_len);   // 组播必须 NON
        } else {
            ESP_LOGE(TAG, "bad multicast addr");
        }
    }
    esp_openthread_lock_release();
}

// ---------------------------------------------------------------------------
// 设备清单周期上报(Task 8)
// ---------------------------------------------------------------------------

static void publish_registry(void) {
    // 注意：entries 数组较大(REGISTRY_MAX_DEVICES × sizeof(bridge_dev_entry_t) ≈ 4KB)，
    // 本函数在 FreeRTOS 定时器服务任务(栈很小，~2KB)中执行，放栈上会触发栈保护错误。
    // 故用堆分配。定时器串行触发，无重入风险。
    bridge_dev_entry_t *entries = calloc(REGISTRY_MAX_DEVICES, sizeof(bridge_dev_entry_t));
    if (entries == NULL) {
        ESP_LOGE(TAG, "registry: alloc failed");
        return;
    }
    size_t count = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    registry_collect(entries, REGISTRY_MAX_DEVICES, &count);
    esp_openthread_lock_release();

    char *json = bridge_registry_to_json(entries, count);
    free(entries);
    if (json == NULL) {
        return;
    }
    if (s_client != NULL) {
        char t[128];
        snprintf(t, sizeof(t), "%s/dev/registry", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
        esp_mqtt_client_publish(s_client, t, json, 0, 0, /*retain=*/1);
        ESP_LOGI(TAG, "published registry: %d devices", (int)count);
    }
    free(json);
}

// 解析 cmd/registry 上的 registry_list 指令，查 SRP 表并发布响应到 cmd/resp。
static void handle_registry_cmd(const char *data, int data_len) {
    // 取 reqid(可选)
    char reqid[64] = "";
    cJSON *root = cJSON_ParseWithLength(data, data_len);
    if (root != NULL) {
        cJSON *jr = cJSON_GetObjectItem(root, "reqid");
        if (cJSON_IsString(jr) && jr->valuestring) {
            strncpy(reqid, jr->valuestring, sizeof(reqid) - 1);
            reqid[sizeof(reqid) - 1] = '\0';
        }
        cJSON_Delete(root);
    }

    bridge_dev_entry_t *entries = calloc(REGISTRY_MAX_DEVICES, sizeof(bridge_dev_entry_t));
    if (entries == NULL) { ESP_LOGE(TAG, "registry cmd: alloc failed"); return; }
    size_t count = 0;
    esp_openthread_lock_acquire(portMAX_DELAY);
    registry_collect(entries, REGISTRY_MAX_DEVICES, &count);
    const char *br_eui = br_eui64_str();
    esp_openthread_lock_release();

    char *json = bridge_registry_list_resp_to_json(reqid, br_eui, entries, count);
    free(entries);
    if (json == NULL) return;
    if (s_client != NULL) {
        char t[128];
        snprintf(t, sizeof(t), "%s/cmd/resp", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
        esp_mqtt_client_publish(s_client, t, json, 0, 0, 0);
        ESP_LOGI(TAG, "registry_list answered: %d devices", (int)count);
    }
    free(json);
}

static void registry_timer_cb(TimerHandle_t t) {
    (void)t;
    publish_registry();
}

// ---------------------------------------------------------------------------
// MQTT 客户端
// ---------------------------------------------------------------------------

static void subscribe_all(esp_mqtt_client_handle_t client) {
    char t[128];
    snprintf(t, sizeof(t), "%s/cmd/unicast/+", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_subscribe(client, t, 0);
    snprintf(t, sizeof(t), "%s/cmd/multicast", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_subscribe(client, t, 0);
    snprintf(t, sizeof(t), "%s/cmd/registry", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
    esp_mqtt_client_subscribe(client, t, 0);
    ESP_LOGI(TAG, "subscribed under prefix '%s'", CONFIG_MQTT_OT_BRIDGE_TOPIC_PREFIX);
}

static void mqtt_event_handler(void *args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        subscribe_all(event->client);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "MQTT disconnected, will retry");
        break;
    case MQTT_EVENT_DATA: {
        char topic[128];
        int tlen = event->topic_len < (int)sizeof(topic) - 1 ? event->topic_len : (int)sizeof(topic) - 1;
        memcpy(topic, event->topic, tlen);
        topic[tlen] = '\0';
        const char *suffix = topic_suffix_after_prefix(topic);
        if (suffix != NULL) {
            if (strcmp(suffix, "registry") == 0) {
                handle_registry_cmd(event->data, event->data_len);
            } else {
                handle_downlink(suffix, event->data, event->data_len);
            }
        }
        break;
    }
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error");
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------

esp_err_t mqtt_ot_bridge_start(void) {
    ESP_RETURN_ON_ERROR(coap_ensure_started(), TAG, "coap start failed");

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_MQTT_OT_BRIDGE_BROKER_URI,
        // mqtts:// 时用嵌入的 CA 校验服务器证书；mqtt:// 时 esp-mqtt 忽略此字段（明文向后兼容）
        .broker.verification.certificate = (const char *)mqtt_ca_cert_pem_start,
        .broker.verification.certificate_len = mqtt_ca_cert_pem_end - mqtt_ca_cert_pem_start,
        .credentials.username = CONFIG_MQTT_OT_BRIDGE_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_OT_BRIDGE_PASSWORD,
    };
    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    ESP_RETURN_ON_ERROR(esp_mqtt_client_start(s_client), TAG, "mqtt start failed");

    s_registry_timer = xTimerCreate("otbr_reg",
                                    pdMS_TO_TICKS(CONFIG_MQTT_OT_BRIDGE_REGISTRY_INTERVAL_S * 1000),
                                    pdTRUE, NULL, registry_timer_cb);
    if (s_registry_timer != NULL) {
        xTimerStart(s_registry_timer, 0);
    }
    return ESP_OK;
}

void mqtt_ot_bridge_stop(void) {
    if (s_registry_timer != NULL) {
        xTimerStop(s_registry_timer, 0);
        xTimerDelete(s_registry_timer, 0);
        s_registry_timer = NULL;
    }
    if (s_client != NULL) {
        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
    }
}
