# Deep Sleep Motion Sensor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a motion-sensor wake source to the `deep_sleep` H2 example so that a 3.3 V sensor pulse wakes the device, which then attaches to Thread, SRP-registers, and unicasts a CoAP event report to the BR `/ack` resource (which `mqtt_ot_bridge` transparently forwards to MQTT `dev/response`).

**Architecture:** Every deep-sleep wake is a cold boot. On boot the wake cause is mapped to an event string (`motion`/`heartbeat`/`boot`). The device attaches; the SRP auto-start callback hands us the BR (SRP server) address, at which point we send one CoAP NON POST to `/ack` with a hand-written JSON payload, then arm a short flush timer that calls `esp_deep_sleep_start()`. A max-awake fallback timer forces sleep if attach/report never completes. The BR and `mqtt_ot_bridge` are unchanged — the new `event` field rides through the existing passthrough.

**Tech Stack:** ESP-IDF v6.0.2, OpenThread (SED/MTD), FreeRTOS + esp_timer, CoAP (`openthread/coap.h`), SRP client (`openthread/srp_client.h`). No cJSON — payload is built with `snprintf`.

## Global Constraints

- **Target chip:** ESP32-H2 (and ESP32-C6 per the example's supported targets); EXT1 wake pin default **GPIO8** (H2 EXT1 legal RTC GPIO range is **8–14**; GPIO7 is RTC but not led out; GPIO9 is BOOT).
- **Build/verify environment:** ESP-IDF must be loaded via `source /d/esp/v6.0.2/esp-idf/export.sh`. On this machine `idf.py` may need to run in an ESP-IDF PowerShell/CMD, not Git Bash — if a build command fails with "MSys/Mingw is no longer supported", the user runs it in PowerShell and pastes back the result.
- **No host unit-test suite for examples.** Verification = `idf.py build` (compile clean, no new warnings) + device monitor. There is no pytest/ceedling cycle here.
- **No new managed components.** Do not add cJSON or any `espressif/*` dependency. Payload JSON is built with `snprintf`.
- **BR contract (must match `mqtt_ot_bridge` config, do not change BR code):** report target CoAP resource `ack`, CoAP port `5683`, SRP service name `_iot._udp`. SRP host name = 16-char lowercase-hex EUI64.
- **`main` component REQUIRES:** `deep_sleep/main` already uses explicit `PRIV_REQUIRES` (it is not an ot_cli-derived MINIMAL_BUILD project). Keep listing components explicitly there.
- **Sleep must not precede a successful send:** never re-introduce the old "enter deep sleep on a fixed 5 s timer after CHILD" logic — it drops the report when `otSrpClientGetServerAddress()` is still NULL.

---

## File Structure

- `deep_sleep/main/Kconfig.projbuild` — **new.** Menu with wake GPIO, heartbeat interval, max-awake fallback, SRP service name, `/ack` URI, CoAP port.
- `deep_sleep/main/device_eui64.c` / `.h` — **new (copied verbatim from `ot_iot_device/main`).** Pure EUI64→hex-string helper. Copied rather than shared to keep the example self-contained (matches how `ot_iot_device` carries its own copy).
- `deep_sleep/main/esp_ot_sleepy_device.c` — **rewritten.** Wake-cause→event mapping, EXT1 high-level motion wake + heartbeat timer wake, SRP register, CoAP report, report-then-sleep + fallback.
- `deep_sleep/main/CMakeLists.txt` — **modified.** Add `device_eui64.c` to SRCS.
- `deep_sleep/README.md` — **modified.** Wiring, event semantics, BR-config-must-match note.

---

## Task 1: Config scaffolding (Kconfig + build wiring + EUI64 helper)

**Files:**
- Create: `deep_sleep/main/Kconfig.projbuild`
- Create: `deep_sleep/main/device_eui64.c` (copy of `ot_iot_device/main/device_eui64.c`)
- Create: `deep_sleep/main/device_eui64.h` (copy of `ot_iot_device/main/device_eui64.h`)
- Modify: `deep_sleep/main/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing (first task).
- Produces:
  - Kconfig symbols: `CONFIG_MOTION_WAKEUP_GPIO` (int), `CONFIG_MOTION_HEARTBEAT_SEC` (int), `CONFIG_MOTION_MAX_AWAKE_MS` (int), `CONFIG_MOTION_SRP_SERVICE_NAME` (string), `CONFIG_MOTION_ACK_URI` (string), `CONFIG_MOTION_COAP_PORT` (int).
  - `void device_eui64_to_string(const uint8_t in[8], char out[17]);` — 8-byte EUI64 → 16-char lowercase hex + NUL.

- [ ] **Step 1: Create the Kconfig menu**

Create `deep_sleep/main/Kconfig.projbuild`:

```kconfig
menu "Deep Sleep Motion Sensor"

    config MOTION_WAKEUP_GPIO
        int "Motion sensor wakeup GPIO"
        default 8
        help
            运动传感器 3.3V 输出接入的 GPIO。EXT1 深睡唤醒仅支持 RTC GPIO,
            ESP32-H2 合法范围为 8-14(GPIO7 虽是 RTC 但未引出,GPIO9 为 BOOT)。
            唤醒条件为高电平(检测到运动=3.3V)。

    config MOTION_HEARTBEAT_SEC
        int "Heartbeat wakeup interval (seconds)"
        default 300
        help
            RTC 定时器唤醒间隔,作为周期性"存活"心跳上报。

    config MOTION_MAX_AWAKE_MS
        int "Max awake fallback (ms)"
        default 10000
        help
            唤醒后最大清醒兜底毫秒数。无论上报是否成功,到点强制回深睡,
            防止 attach/上报失败时无限清醒耗电。

    config MOTION_SRP_SERVICE_NAME
        string "SRP service name"
        default "_iot._udp"
        help
            通过 SRP 注册的服务类型名,须与 BR mqtt_ot_bridge 约定一致。

    config MOTION_ACK_URI
        string "BR uplink CoAP resource path"
        default "ack"
        help
            上报目标:BR 的 CoAP 资源,必须与 BR 的 MQTT_OT_BRIDGE_ACK_URI 一致。

    config MOTION_COAP_PORT
        int "CoAP port"
        default 5683
        help
            CoAP 端口,须与 BR mqtt_ot_bridge 一致。

endmenu
```

- [ ] **Step 2: Copy the EUI64 helper source**

Copy `ot_iot_device/main/device_eui64.c` to `deep_sleep/main/device_eui64.c` verbatim. Its content is:

```c
#include "device_eui64.h"

void device_eui64_to_string(const uint8_t in[8], char out[17]) {
    static const char *hexd = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i * 2]     = hexd[(in[i] >> 4) & 0xf];
        out[i * 2 + 1] = hexd[in[i] & 0xf];
    }
    out[16] = '\0';
}
```

- [ ] **Step 3: Copy the EUI64 helper header**

Copy `ot_iot_device/main/device_eui64.h` to `deep_sleep/main/device_eui64.h` verbatim. Its content is:

```c
#pragma once
#include <stdint.h>

// 8 字节 EUI64 -> 16 位小写十六进制 + '\0'(out 至少 17 字节)。首字节 -> out[0..1]。
void device_eui64_to_string(const uint8_t in[8], char out[17]);
```

- [ ] **Step 4: Add the new source to CMakeLists**

Replace the contents of `deep_sleep/main/CMakeLists.txt` with:

```cmake
idf_component_register(SRCS "esp_ot_sleepy_device.c" "device_eui64.c"
                       PRIV_REQUIRES esp_event esp_timer openthread nvs_flash driver esp_hw_support
                       INCLUDE_DIRS ".")
```

(`driver` was already implied via `driver/rtc_io.h`; `esp_hw_support` provides `esp_random.h` used in Task 3. Adding them now avoids a second CMake edit.)

- [ ] **Step 5: Build to verify config + scaffolding compile**

Run (after `source /d/esp/v6.0.2/esp-idf/export.sh`, from `deep_sleep/`):
```bash
idf.py set-target esp32h2 && idf.py build
```
Expected: build SUCCEEDS. `device_eui64.c` compiles; new `CONFIG_MOTION_*` symbols appear in `sdkconfig` (unused so far — no error). If the build fails with an MSys/Mingw message, run the same command in ESP-IDF PowerShell.

- [ ] **Step 6: Commit**

```bash
git add deep_sleep/main/Kconfig.projbuild deep_sleep/main/device_eui64.c deep_sleep/main/device_eui64.h deep_sleep/main/CMakeLists.txt deep_sleep/sdkconfig.defaults
git commit -m "feat(deep_sleep): add motion-sensor Kconfig + EUI64 helper scaffolding

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 2: Wake sources + event mapping (motion EXT1 high, heartbeat, fallback sleep)

This task replaces the wake-source configuration and the sleep-trigger logic, but does **not** yet send any report. After this task the device: wakes on GPIO8 going high **or** on the heartbeat timer, prints the mapped event string, and returns to deep sleep via the max-awake fallback timer. This is independently observable in monitor.

**Files:**
- Modify: `deep_sleep/main/esp_ot_sleepy_device.c` (rewrite)

**Interfaces:**
- Consumes: `CONFIG_MOTION_WAKEUP_GPIO`, `CONFIG_MOTION_HEARTBEAT_SEC`, `CONFIG_MOTION_MAX_AWAKE_MS` (Task 1).
- Produces (used by Task 3):
  - `static const char *s_event;` — event string set during init: `"motion"`, `"heartbeat"`, or `"boot"`.
  - `static char s_eui64_str[17];` — filled in Task 3.
  - `static void enter_deep_sleep(void);` — records sleep-enter time and calls `esp_deep_sleep_start()`.
  - `static esp_timer_handle_t s_max_awake_timer;` — one-shot fallback started in `app_main`.

- [ ] **Step 1: Rewrite esp_ot_sleepy_device.c with wake/event/fallback logic**

Replace the entire contents of `deep_sleep/main/esp_ot_sleepy_device.c` with:

```c
/*
 * SPDX-FileCopyrightText: 2023-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread Deep-Sleep Motion Sensor Example.
 * Wakes on a motion sensor (3.3V high on GPIO, EXT1) or a periodic heartbeat
 * timer, attaches to Thread, SRP-registers, and unicasts a CoAP event report
 * to the BR /ack resource (forwarded to MQTT by mqtt_ot_bridge).
 */

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/time.h>
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_openthread.h"
#include "esp_openthread_netif_glue.h"
#include "esp_ot_sleepy_device_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "driver/rtc_io.h"

#if !SOC_IEEE802154_SUPPORTED
#error "Openthread sleepy device is only supported for the SoCs which have IEEE 802.15.4 module"
#endif

#define TAG "ot_esp_power_save"

static RTC_DATA_ATTR struct timeval s_sleep_enter_time;
static esp_timer_handle_t s_max_awake_timer;

// 本次唤醒对应的事件类型,由唤醒原因映射而来。Task 3 上报时读取。
static const char *s_event = "boot";
// EUI64 十六进制字符串,Task 3 填充与使用。
static char s_eui64_str[17];

static void create_config_network(otInstance *instance)
{
    otLinkModeConfig linkMode = { 0 };

    linkMode.mRxOnWhenIdle = false;
    linkMode.mDeviceType = false;
    linkMode.mNetworkData = false;

    if (otLinkSetPollPeriod(instance, CONFIG_OPENTHREAD_NETWORK_POLLPERIOD_TIME) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set OpenThread pollperiod.");
        abort();
    }

    if (otThreadSetLinkMode(instance, linkMode) != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "Failed to set OpenThread linkmode.");
        abort();
    }
    ESP_ERROR_CHECK(esp_openthread_auto_start(NULL));
}

// 记录进睡时间并进入深睡。esp_deep_sleep_start() 不返回,故多路径调用天然互斥。
static void enter_deep_sleep(void)
{
    ESP_LOGI(TAG, "Enter deep sleep");
    gettimeofday(&s_sleep_enter_time, NULL);
    esp_deep_sleep_start();
}

// 最大清醒兜底定时器回调:无论上报是否完成,到点强制回深睡。
static void max_awake_timer_cb(void *arg)
{
    (void)arg;
    ESP_LOGW(TAG, "Max awake fallback reached, sleeping without confirmed report");
    enter_deep_sleep();
}

static void ot_deep_sleep_init(void)
{
    // 打印唤醒原因,并据此确定本次事件类型 s_event。
    struct timeval now;
    gettimeofday(&now, NULL);
    int sleep_time_ms = (now.tv_sec - s_sleep_enter_time.tv_sec) * 1000 + (now.tv_usec - s_sleep_enter_time.tv_usec) / 1000;

    uint32_t wake_up_causes = esp_sleep_get_wakeup_causes();
    if (wake_up_causes & BIT(ESP_SLEEP_WAKEUP_UNDEFINED)) {
        ESP_LOGI(TAG, "Not a deep sleep reset");
        s_event = "boot";
    } else {
        if (wake_up_causes & BIT(ESP_SLEEP_WAKEUP_EXT1)) {
            ESP_LOGI(TAG, "Wake up from GPIO (motion). Time spent in deep sleep and boot: %dms", sleep_time_ms);
            s_event = "motion";
        } else if (wake_up_causes & BIT(ESP_SLEEP_WAKEUP_TIMER)) {
            ESP_LOGI(TAG, "Wake up from timer (heartbeat). Time spent in deep sleep and boot: %dms", sleep_time_ms);
            s_event = "heartbeat";
        }
    }
    ESP_LOGI(TAG, "Event for this wake: %s", s_event);

    // 唤醒源 1:RTC 定时器心跳
    const int wakeup_time_sec = CONFIG_MOTION_HEARTBEAT_SEC;
    ESP_LOGI(TAG, "Enabling timer wakeup, %ds", wakeup_time_sec);
    ESP_ERROR_CHECK(esp_sleep_enable_timer_wakeup((uint64_t)wakeup_time_sec * 1000000ULL));

    // 唤醒源 2:运动传感器 EXT1,高电平触发(检测到运动=3.3V)
    const int gpio_wakeup_pin = CONFIG_MOTION_WAKEUP_GPIO;
    const uint64_t gpio_wakeup_pin_mask = 1ULL << gpio_wakeup_pin;
    ESP_ERROR_CHECK(esp_sleep_enable_ext1_wakeup_io(gpio_wakeup_pin_mask, ESP_EXT1_WAKEUP_ANY_HIGH));

    // 运动传感器多为推挽输出,空闲为低电平:启用下拉、禁用上拉,匹配高电平唤醒。
    // (若传感器为开漏输出,需外部下拉电阻;见 README 接线说明。)
    ESP_ERROR_CHECK(gpio_pulldown_en(gpio_wakeup_pin));
    ESP_ERROR_CHECK(gpio_pullup_dis(gpio_wakeup_pin));
}

void app_main(void)
{
    // Used eventfds:
    // * netif
    // * ot task queue
    // * radio driver
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    ot_deep_sleep_init();

    // 最大清醒兜底:无论后续上报成功与否,到点强制回深睡。
    const esp_timer_create_args_t max_awake_timer_args = {
        .callback = &max_awake_timer_cb,
        .name = "max-awake",
    };
    ESP_ERROR_CHECK(esp_timer_create(&max_awake_timer_args, &s_max_awake_timer));
    ESP_ERROR_CHECK(esp_timer_start_once(s_max_awake_timer, (uint64_t)CONFIG_MOTION_MAX_AWAKE_MS * 1000ULL));

    static esp_openthread_config_t config = {
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };
    ESP_ERROR_CHECK(esp_openthread_start(&config));
    esp_netif_set_default_netif(esp_openthread_get_netif());

    create_config_network(esp_openthread_get_instance());
}
```

Note what changed vs. the original: removed `s_oneshot_timer` and `ot_state_change_callback` (the fixed-5s-after-CHILD sleep); heartbeat interval now `CONFIG_MOTION_HEARTBEAT_SEC`; GPIO wake is now EXT1 **ANY_HIGH** on `CONFIG_MOTION_WAKEUP_GPIO` with pulldown enabled (was ANY_LOW + pullup on GPIO9); added `enter_deep_sleep()` and the max-awake fallback timer; `s_event`/`s_eui64_str` declared for Task 3.

- [ ] **Step 2: Build**

Run (from `deep_sleep/`):
```bash
idf.py build
```
Expected: build SUCCEEDS, no new warnings. `otSetStateChangedCallback` and `esp_sleep_enable_ext1_wakeup_io` resolve.

- [ ] **Step 3: Monitor to verify wake + event mapping + fallback sleep**

Flash and monitor (from `deep_sleep/`, requires a leader on the network per README):
```bash
idf.py -p <PORT> erase-flash flash monitor
```
Expected observations:
- First boot log: `Not a deep sleep reset` and `Event for this wake: boot`, then `Enabling timer wakeup, 300s`.
- After `CONFIG_MOTION_MAX_AWAKE_MS` (~10 s) with no report yet: `Max awake fallback reached...` then `Enter deep sleep`.
- Pulling GPIO8 to 3.3 V (or the sensor firing) wakes it: `Wake up from GPIO (motion)` and `Event for this wake: motion`.
- Waiting 300 s wakes it with `Wake up from timer (heartbeat)` / `Event for this wake: heartbeat`.

(If the device never sleeps because it can't attach, that's expected until a leader exists — the fallback still fires and sleeps it.)

- [ ] **Step 4: Commit**

```bash
git add deep_sleep/main/esp_ot_sleepy_device.c
git commit -m "feat(deep_sleep): motion EXT1 high-level wake + heartbeat + event mapping

Replace fixed-5s-after-CHILD sleep with a max-awake fallback; map wake
cause to motion/heartbeat/boot event string. No uplink yet.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 3: SRP register + CoAP event report + report-then-sleep

This task adds the uplink: SRP registration (for BR registry/routing), and — driven by the SRP auto-start callback that supplies the BR server address — one CoAP NON POST to `/ack` carrying `{"id","reqid","event"}`, followed by a short flush delay then deep sleep. The max-awake fallback from Task 2 remains as the failure path.

**Files:**
- Modify: `deep_sleep/main/esp_ot_sleepy_device.c`

**Interfaces:**
- Consumes: `s_event`, `s_eui64_str`, `enter_deep_sleep()` (Task 2); `CONFIG_MOTION_SRP_SERVICE_NAME`, `CONFIG_MOTION_ACK_URI`, `CONFIG_MOTION_COAP_PORT` (Task 1); `device_eui64_to_string()` (Task 1).
- Produces: end-to-end MQTT event on `<prefix>/dev/response`.

- [ ] **Step 1: Add includes for SRP/CoAP/random/lock**

In `deep_sleep/main/esp_ot_sleepy_device.c`, add these includes to the existing include block (after `#include "driver/rtc_io.h"`):

```c
#include "esp_random.h"
#include "esp_openthread_lock.h"
#include "device_eui64.h"
#include "openthread/instance.h"
#include "openthread/thread.h"
#include "openthread/srp_client.h"
#include "openthread/srp_client_buffers.h"
#include "openthread/platform/radio.h"
#include "openthread/coap.h"
#include "openthread/message.h"
#include "openthread/ip6.h"
```

- [ ] **Step 2: Add a flush-delay constant and report-flush timer handle**

In the static-variables section (near `s_max_awake_timer`), add:

```c
// 上报发出后等待 radio flush 再进深睡的时长。NON 报文无 ACK,故用固定短延时兜住发送。
#define REPORT_FLUSH_MS 1000
static esp_timer_handle_t s_flush_timer;
```

- [ ] **Step 3: Add the flush timer callback (sleeps after report flush)**

Add above `ot_deep_sleep_init` (after `max_awake_timer_cb`):

```c
// 上报发出后经短暂 flush 延时再进深睡。
static void flush_timer_cb(void *arg)
{
    (void)arg;
    enter_deep_sleep();
}
```

- [ ] **Step 4: Add SRP registration**

Add above `app_main` (after `flush_timer_cb`). This is the `ot_iot_device` `srp_register`, minus the CoAP-server port coupling (this example does not run a `/ctrl` server, but we keep a service entry so the BR registry records the device; the port is set to `CONFIG_MOTION_COAP_PORT` for consistency). Caller must hold the OT lock.

```c
// 调用者必须已持 OT 锁。以 EUI64 为 host/instance 名做 SRP 自动注册。
static void srp_register(otInstance *inst)
{
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
    strncpy(svc_name, CONFIG_MOTION_SRP_SERVICE_NAME, size - 1);
    svc_name[size - 1] = '\0';

    entry->mService.mPort = CONFIG_MOTION_COAP_PORT;

    otError err = otSrpClientAddService(inst, &entry->mService);
    if (err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "SRP add service err=%d", err);
        return;
    }
    ESP_LOGI(TAG, "SRP registration queued: host/instance=%s service=%s",
             s_eui64_str, CONFIG_MOTION_SRP_SERVICE_NAME);
}
```

- [ ] **Step 5: Add the event report (CoAP NON POST to /ack)**

Add after `srp_register`. Caller must hold the OT lock (the SRP auto-start callback runs in OT task context with the lock held). Builds JSON with `snprintf`, self-generates `reqid` from `esp_random()`.

```c
// 构造事件 JSON 并单播 NON POST 到 BR(SRP server)的 /ack 资源。
// 调用者必须已持 OT 锁。返回是否成功交给发送。
static bool device_report(otInstance *inst, const char *event)
{
    const otSockAddr *server = otSrpClientGetServerAddress(inst);
    if (server == NULL) {
        ESP_LOGW(TAG, "report: no SRP server address yet");
        return false;
    }

    char json[128];
    unsigned reqid = esp_random();
    int n = snprintf(json, sizeof(json),
                     "{\"id\":\"%s\",\"reqid\":\"%08x\",\"event\":\"%s\"}",
                     s_eui64_str, reqid, event);
    if (n <= 0 || n >= (int)sizeof(json)) {
        ESP_LOGE(TAG, "report: json build failed");
        return false;
    }

    otMessage *msg = otCoapNewMessage(inst, NULL);
    if (msg == NULL) {
        return false;
    }
    otCoapMessageInit(msg, OT_COAP_TYPE_NON_CONFIRMABLE, OT_COAP_CODE_POST);
    otCoapMessageAppendUriPathOptions(msg, CONFIG_MOTION_ACK_URI);
    otCoapMessageSetPayloadMarker(msg);
    otMessageAppend(msg, json, (uint16_t)strlen(json));

    otMessageInfo info;
    memset(&info, 0, sizeof(info));
    info.mPeerAddr = server->mAddress;
    info.mPeerPort = CONFIG_MOTION_COAP_PORT;

    otError err = otCoapSendRequest(inst, msg, &info, NULL, NULL);
    if (err != OT_ERROR_NONE) {
        otMessageFree(msg);
        ESP_LOGE(TAG, "report send err=%d", err);
        return false;
    }
    ESP_LOGI(TAG, "reported event=%s reqid=%08x", event, reqid);
    return true;
}
```

- [ ] **Step 6: Add the SRP auto-start callback that reports then arms sleep**

Add after `device_report`. This fires (in OT context, lock held) when the SRP client discovers the server — i.e. when we have the BR address. It sends the report and arms the flush timer to sleep.

```c
// SRP 自动启动回调:发现 server(即拿到 BR 地址)后上报事件并排定进睡。
// 在 OpenThread 任务上下文调用(已持锁)。
static void srp_autostart_cb(const otSockAddr *server, void *ctx)
{
    (void)ctx;
    if (server == NULL) {
        // server 停止,忽略。
        return;
    }
    otInstance *inst = esp_openthread_get_instance();
    ESP_LOGI(TAG, "SRP auto-start: server found, host=%s", s_eui64_str);

    if (device_report(inst, s_event)) {
        // 报文已交给发送:等待短暂 flush 后进深睡。
        esp_timer_start_once(s_flush_timer, (uint64_t)REPORT_FLUSH_MS * 1000ULL);
    }
    // 若发送失败,依赖 Task 2 的 max-awake 兜底定时器进深睡。
}
```

- [ ] **Step 7: Create the flush timer and enable SRP + CoAP in app_main**

In `app_main`, create the flush timer alongside the max-awake timer. After the existing max-awake timer creation/start block, add:

```c
    const esp_timer_create_args_t flush_timer_args = {
        .callback = &flush_timer_cb,
        .name = "report-flush",
    };
    ESP_ERROR_CHECK(esp_timer_create(&flush_timer_args, &s_flush_timer));
```

Then, after `create_config_network(esp_openthread_get_instance());` at the end of `app_main`, add the SRP + CoAP startup:

```c
    // SRP 注册 + CoAP 客户端启动 + 注册自动启动回调(server 找到即上报)。
    esp_openthread_lock_acquire(portMAX_DELAY);
    otInstance *inst = esp_openthread_get_instance();
    otError coap_err = otCoapStart(inst, CONFIG_MOTION_COAP_PORT);
    if (coap_err != OT_ERROR_NONE) {
        ESP_LOGE(TAG, "CoAP start err=%d", coap_err);
    }
    srp_register(inst);
    otSrpClientEnableAutoStartMode(inst, srp_autostart_cb, NULL);
    esp_openthread_lock_release();
```

- [ ] **Step 8: Build**

Run (from `deep_sleep/`):
```bash
idf.py build
```
Expected: build SUCCEEDS, no new warnings. All `otSrpClient*`, `otCoap*`, `esp_random`, `device_eui64_to_string` symbols resolve.

- [ ] **Step 9: Monitor + MQTT to verify end-to-end report**

With a BR running `mqtt_ot_bridge` on the network and an MQTT subscriber on `<prefix>/dev/response`, flash and monitor (from `deep_sleep/`):
```bash
idf.py -p <PORT> erase-flash flash monitor
```
Expected:
- Device attaches → `SRP auto-start: server found` → `reported event=boot reqid=...` → (`~1 s`) `Enter deep sleep`.
- MQTT subscriber receives `{"id":"<eui64>","reqid":"<8hex>","event":"boot"}` on `<prefix>/dev/response`.
- Pull GPIO8 high (or trigger sensor) → device wakes, reports `event=motion`; MQTT shows `"event":"motion"`.
- After 300 s → `event=heartbeat`; MQTT shows `"event":"heartbeat"`.

- [ ] **Step 10: Commit**

```bash
git add deep_sleep/main/esp_ot_sleepy_device.c
git commit -m "feat(deep_sleep): SRP register + CoAP event report to BR /ack, sleep after send

SRP auto-start callback supplies the BR address; send one NON POST with
{id,reqid,event} then flush-delay and deep sleep. Max-awake timer covers
attach/report failure. BR mqtt_ot_bridge unchanged.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 4: Documentation

**Files:**
- Modify: `deep_sleep/README.md`

**Interfaces:**
- Consumes: final behavior + Kconfig names from Tasks 1–3.
- Produces: user-facing wiring + event + config docs.

- [ ] **Step 1: Add wiring + behavior sections to README**

Insert the following block into `deep_sleep/README.md` immediately after the top `# OpenThread Sleepy Device Example` heading paragraph (before `## How to use example`):

```markdown
## Motion Sensor Event Reporting

This variant adds a **motion sensor wake source**. When the sensor pulls the
wakeup GPIO to 3.3 V, the device wakes from deep sleep, attaches to Thread,
SRP-registers, and unicasts a CoAP NON POST to the BR `/ack` resource, which
`mqtt_ot_bridge` forwards to MQTT `<prefix>/dev/response`. The device also
wakes periodically (heartbeat) and on first boot.

**Report payload:** `{"id":"<eui64>","reqid":"<8-hex>","event":"<event>"}`
where `event` is one of `motion` (EXT1 wake), `heartbeat` (timer wake), or
`boot` (power-on).

### Wiring

Three wires from the motion sensor (e.g. HC-SR501 PIR or RCWL-0516 radar):

| Sensor | ESP32-H2 | Notes |
| ------ | -------- | ----- |
| VCC    | 3V3 / 5V | Per sensor module (PIR often 5 V). |
| GND    | GND      | **Must share ground with the H2.** |
| OUT    | GPIO8    | `CONFIG_MOTION_WAKEUP_GPIO`. Must be an H2 EXT1 RTC GPIO (8–14). |

- The sensor OUT **high level must be 3.3 V** (not 5 V) — the H2 GPIO is not
  5 V tolerant. Measure it before connecting.
- Idle state must be **low** (wake is `ESP_EXT1_WAKEUP_ANY_HIGH`). Push-pull
  PIR/radar outputs need no external resistor. An **open-drain** output floats
  when idle and requires an external pull-down (e.g. 100 kΩ to GND).

### Configuration (`idf.py menuconfig` → "Deep Sleep Motion Sensor")

| Symbol | Default | Meaning |
| ------ | ------- | ------- |
| `MOTION_WAKEUP_GPIO` | 8 | Sensor OUT pin (H2 EXT1 range 8–14). |
| `MOTION_HEARTBEAT_SEC` | 300 | Heartbeat wake interval. |
| `MOTION_MAX_AWAKE_MS` | 10000 | Force sleep if attach/report never completes. |
| `MOTION_SRP_SERVICE_NAME` | `_iot._udp` | Must match BR `mqtt_ot_bridge`. |
| `MOTION_ACK_URI` | `ack` | Must match BR `MQTT_OT_BRIDGE_ACK_URI`. |
| `MOTION_COAP_PORT` | 5683 | Must match BR `mqtt_ot_bridge`. |

> The `ACK_URI`, CoAP port, and SRP service name **must match the BR's
> `mqtt_ot_bridge` configuration**, or reports will not reach MQTT.
```

- [ ] **Step 2: Commit**

```bash
git add deep_sleep/README.md
git commit -m "docs(deep_sleep): document motion sensor wiring, events, and config

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

## Task 5: Hardware validation (checklist, no code)

This task has no code changes; it records the acceptance checks against the spec scenarios. Perform on hardware with a BR running `mqtt_ot_bridge`.

- [ ] **Step 1: Build clean for target**

Run (from `deep_sleep/`): `idf.py set-target esp32h2 && idf.py build` → SUCCEEDS, no new warnings.

- [ ] **Step 2: Motion event**

Trigger the sensor (or manually drive GPIO8 to 3.3 V). Monitor shows `Wake up from GPIO (motion)` → `reported event=motion`. MQTT `<prefix>/dev/response` receives `"event":"motion"`. Device returns to deep sleep (`Enter deep sleep`).

- [ ] **Step 3: Heartbeat event**

Leave idle for `MOTION_HEARTBEAT_SEC` (300 s). Monitor shows `Wake up from timer (heartbeat)` → `reported event=heartbeat`. MQTT receives `"event":"heartbeat"`.

- [ ] **Step 4: Fallback on network failure**

Power off the BR (or remove from network). Wake the device. Within `MOTION_MAX_AWAKE_MS` monitor shows `Max awake fallback reached...` → `Enter deep sleep`. Device does not stay awake indefinitely.

- [ ] **Step 5: reqid uniqueness**

Trigger two consecutive reports; confirm the `reqid` field differs between them in the MQTT messages.

- [ ] **Step 6: Update OpenSpec task checkboxes**

Mark the corresponding items in `openspec/changes/add-motion-sensor/tasks.md` complete, then run `openspec validate add-motion-sensor --json` (expect valid). Archive later with `/opsx:archive` once merged.

---

## Self-Review

**1. Spec coverage** (against `openspec/changes/add-motion-sensor/spec.md`):
- *运动传感器 EXT1 唤醒* → Task 2 Step 1 (`esp_sleep_enable_ext1_wakeup_io`, ANY_HIGH, `CONFIG_MOTION_WAKEUP_GPIO`). ✓
- *定时心跳唤醒* → Task 2 Step 1 (`CONFIG_MOTION_HEARTBEAT_SEC`). ✓
- *唤醒原因映射为事件类型* → Task 2 Step 1 (EXT1/TIMER/UNDEFINED → motion/heartbeat/boot). ✓
- *唤醒后 SRP 注册并上报事件* → Task 3 Steps 4–7 (`srp_register`, `device_report`, autostart cb, hand-written JSON, self-gen reqid). ✓
- *上报完成后再进深睡* → Task 3 Steps 3,6,7 (flush timer) + Task 2 fallback timer. ✓
- *不改动 BR 上行透传* → No BR files in any task; verified passthrough in exploration. ✓

**2. Placeholder scan:** No TBD/TODO/"handle edge cases"/"similar to Task N". All code steps contain full code. ✓

**3. Type consistency:**
- `s_event` (`const char *`) set in Task 2, read in Task 3 `srp_autostart_cb`/`device_report`. ✓
- `s_eui64_str[17]` declared Task 2, filled in `srp_register` (Task 3), read in `device_report`. ✓
- `enter_deep_sleep(void)` defined Task 2, called by `max_awake_timer_cb` (Task 2) and `flush_timer_cb` (Task 3). ✓
- `device_report(otInstance*, const char*)` → `bool`, called by `srp_autostart_cb` with return checked. ✓
- `device_eui64_to_string(const uint8_t[8], char[17])` — signature identical to copied header. ✓
- Kconfig symbols `CONFIG_MOTION_*` — names identical between Task 1 definition and Tasks 2/3 use. ✓

Adaptation note: this codebase has no host unit-test harness for examples (per `CLAUDE.md`), so tasks use `idf.py build` + monitor as the verification cycle instead of pytest-style red/green. This is intentional, not a placeholder.
