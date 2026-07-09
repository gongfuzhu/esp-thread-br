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
