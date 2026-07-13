#include "iot_cap_servo_set.h"
#include "iot_device_core.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define TAG "iot_cap_servo_set"
#define SERVO_MODE       LEDC_LOW_SPEED_MODE
#define SERVO_RES        LEDC_TIMER_10_BIT       // 0..1023
#define SERVO_MAX_DUTY   1023
#define SERVO_TIMER      LEDC_TIMER_1
#define SERVO_FREQ       50  // 50Hz for servo PWM

// gpio→channel 分配表(组件级 static 状态)。
static int s_gpio_of_channel[LEDC_CHANNEL_MAX];
static bool s_channel_used[LEDC_CHANNEL_MAX];
static bool s_timer_ready = false;

static int alloc_channel(int gpio) {
    for (int c = 0; c < LEDC_CHANNEL_MAX; c++) {
        if (s_channel_used[c] && s_gpio_of_channel[c] == gpio) return c;  // 复用
    }
    for (int c = 0; c < LEDC_CHANNEL_MAX; c++) {
        if (!s_channel_used[c]) { s_channel_used[c] = true; s_gpio_of_channel[c] = gpio; return c; }
    }
    return -1;   // 耗尽
}

static int servo_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jgpio = cJSON_GetObjectItem(data, "gpio");
    cJSON *jangle = cJSON_GetObjectItem(data, "angle");
    if (!cJSON_IsNumber(jgpio) || !cJSON_IsNumber(jangle)) {
        return IOT_CODE_PARAM;
    }
    int gpio = jgpio->valueint;
    int angle = jangle->valueint;
    if (angle < 0 || angle > 180) {
        return IOT_CODE_PARAM;
    }

    if (!s_timer_ready) {
        ledc_timer_config_t tc = {
            .speed_mode = SERVO_MODE, .duty_resolution = SERVO_RES,
            .timer_num = SERVO_TIMER, .freq_hz = SERVO_FREQ, .clk_cfg = LEDC_AUTO_CLK,
        };
        if (ledc_timer_config(&tc) != ESP_OK) return IOT_CODE_HW;
        s_timer_ready = true;
    }

    int ch = alloc_channel(gpio);
    if (ch < 0) return IOT_CODE_HW;   // 通道耗尽

    // Calculate duty: 0.5ms @0° →26, 2.5ms@180°→128
    uint32_t duty = 26 + ((128 - 26) * angle) / 180;
    ledc_channel_config_t cc = {
        .gpio_num = gpio, .speed_mode = SERVO_MODE, .channel = ch,
        .timer_sel = SERVO_TIMER, .duty = duty, .hpoint = 0, .intr_type = LEDC_INTR_DISABLE,
    };
    if (ledc_channel_config(&cc) != ESP_OK) return IOT_CODE_HW;
    ledc_set_duty(SERVO_MODE, ch, duty);
    ledc_update_duty(SERVO_MODE, ch);

    cJSON_AddNumberToObject(resp_data, "gpio", gpio);
    cJSON_AddNumberToObject(resp_data, "angle", angle);
    return IOT_CODE_OK;
}

void iot_cap_servo_set_init(void) {
    if (!iot_device_register_handler("servo_set", servo_handler)) {
        ESP_LOGE(TAG, "register servo_set failed");
    }
}
