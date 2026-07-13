/*
 * SPDX-FileCopyrightText: 2021-2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: CC0-1.0
 *
 * OpenThread IoT Device Example (switch/LED), controlled via mqtt_ot_bridge.
 */

#include <stdio.h>
#include <unistd.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_ot_config.h"
#include "esp_vfs_eventfd.h"
#include "nvs_flash.h"
#include "ot_examples_common.h"

#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
#include "ot_led_strip.h"
#endif

#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
#include "esp_ot_cli_extension.h"
#endif

#include "iot_device_core.h"
#include "iot_cap_switch.h"
#include "iot_cap_pwm_set.h"
#include "iot_cap_adc_read.h"
#include "iot_cap_servo_set.h"

#define TAG "ot_iot_device"

void app_main(void)
{
    esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
#if CONFIG_OPENTHREAD_PLATFORM_NETIF
    ESP_ERROR_CHECK(esp_netif_init());
#endif
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));

#if CONFIG_OPENTHREAD_CLI
    ot_console_start();
    ot_register_external_commands();
#endif

    static esp_openthread_config_t config = {
#if CONFIG_OPENTHREAD_PLATFORM_NETIF
        .netif_config = ESP_NETIF_DEFAULT_OPENTHREAD(),
#endif
        .platform_config = {
            .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
            .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
            .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
        },
    };

    ESP_ERROR_CHECK(esp_openthread_start(&config));
#if CONFIG_OPENTHREAD_CLI_ESP_EXTENSION
    esp_cli_custom_command_init();
#endif
#if CONFIG_OPENTHREAD_STATE_INDICATOR_ENABLE
    ESP_ERROR_CHECK(esp_openthread_state_indicator_init(esp_openthread_get_instance()));
#endif
#if CONFIG_OPENTHREAD_NETWORK_AUTO_START
    ot_network_auto_start();
#endif

    iot_device_core_start();
    iot_cap_switch_init();
    iot_cap_pwm_set_init();
    iot_cap_adc_read_init();
    iot_cap_servo_set_init();
}
