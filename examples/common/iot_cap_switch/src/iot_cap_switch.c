#include "iot_cap_switch.h"
#include "iot_device_core.h"
#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#define TAG "iot_cap_switch"

// hold 自动回复：单槽(命令低频)。定时器承载，不阻塞 worker 过久。
static TimerHandle_t s_hold_timer = NULL;
static int s_hold_gpio = -1;
static int s_hold_restore_level = 0;

static void hold_timer_cb(TimerHandle_t t) {
    (void)t;
    if (s_hold_gpio >= 0) {
        gpio_set_level(s_hold_gpio, s_hold_restore_level);
    }
}

static void ensure_output(int gpio) {
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT_OUTPUT,   // INPUT_OUTPUT 便于回读
        .pull_up_en = 0, .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

static int switch_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jgpio = cJSON_GetObjectItem(data, "gpio");
    cJSON *jaction = cJSON_GetObjectItem(data, "action");
    if (!cJSON_IsNumber(jgpio) || !cJSON_IsString(jaction)) {
        return IOT_CODE_PARAM;
    }
    int gpio = jgpio->valueint;
    int level;
    if (strcmp(jaction->valuestring, "on") == 0) {
        level = 1;
    } else if (strcmp(jaction->valuestring, "off") == 0) {
        level = 0;
    } else {
        return IOT_CODE_PARAM;
    }

    cJSON *jdelay = cJSON_GetObjectItem(data, "delay");
    cJSON *jhold  = cJSON_GetObjectItem(data, "hold");
    int delay_ms = cJSON_IsNumber(jdelay) ? jdelay->valueint : 0;
    int hold_ms  = cJSON_IsNumber(jhold)  ? jhold->valueint  : 0;

    ensure_output(gpio);
    if (delay_ms > 0) {
        vTaskDelay(pdMS_TO_TICKS(delay_ms));   // 在 worker 任务里，允许阻塞
    }
    gpio_set_level(gpio, level);

    if (hold_ms > 0) {
        s_hold_gpio = gpio;
        s_hold_restore_level = level ? 0 : 1;   // 回到相反态
        if (s_hold_timer == NULL) {
            s_hold_timer = xTimerCreate("sw_hold", pdMS_TO_TICKS(hold_ms), pdFALSE, NULL, hold_timer_cb);
        } else {
            xTimerChangePeriod(s_hold_timer, pdMS_TO_TICKS(hold_ms), 0);
        }
        if (s_hold_timer != NULL) xTimerStart(s_hold_timer, 0);
    }

    cJSON_AddNumberToObject(resp_data, "gpio", gpio);
    cJSON_AddStringToObject(resp_data, "status", level ? "on" : "off");
    return IOT_CODE_OK;
}

void iot_cap_switch_init(void) {
    if (!iot_device_register_handler("switch", switch_handler)) {
        ESP_LOGE(TAG, "register switch failed");
    }
}