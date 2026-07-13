#include "iot_cap_servo_set.h"
#include "iot_device_core.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define TAG "iot_cap_servo_set"
#define SERVO_MODE       LEDC_LOW_SPEED_MODE
#define SERVO_RES        LEDC_TIMER_13_BIT     // 0..8191
#define SERVO_MAX_DUTY   8191
#define SERVO_TIMER      LEDC_TIMER_1          // 独占，区别于 pwm_set 的 TIMER_0
#define SERVO_FREQ_HZ    50                    // 20ms 周期
#define SERVO_PERIOD_US  20000                 // 1e6 / 50Hz
#define SERVO_MIN_US     500                   // 0°   → 0.5ms
#define SERVO_MAX_US     2500                  // 180° → 2.5ms
#define SERVO_ANGLE_MAX  180

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

// angle(0..180) → 脉宽(us) → 占空比 raw(0..8191)。
static uint32_t angle_to_duty(int angle) {
    int pulse_us = SERVO_MIN_US + (SERVO_MAX_US - SERVO_MIN_US) * angle / SERVO_ANGLE_MAX;
    return (uint32_t)((uint64_t)pulse_us * SERVO_MAX_DUTY / SERVO_PERIOD_US);
}

static int servo_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jgpio = cJSON_GetObjectItem(data, "gpio");
    cJSON *jangle = cJSON_GetObjectItem(data, "angle");
    if (!cJSON_IsNumber(jgpio) || !cJSON_IsNumber(jangle)) {
        return IOT_CODE_PARAM;
    }
    int gpio = jgpio->valueint;
    int angle = jangle->valueint;
    if (angle < 0 || angle > SERVO_ANGLE_MAX) {
        return IOT_CODE_PARAM;
    }

    if (!s_timer_ready) {
        ledc_timer_config_t tc = {
            .speed_mode = SERVO_MODE, .duty_resolution = SERVO_RES,
            .timer_num = SERVO_TIMER, .freq_hz = SERVO_FREQ_HZ, .clk_cfg = LEDC_AUTO_CLK,
        };
        if (ledc_timer_config(&tc) != ESP_OK) return IOT_CODE_HW;
        s_timer_ready = true;
    }

    int ch = alloc_channel(gpio);
    if (ch < 0) return IOT_CODE_HW;   // 通道耗尽

    uint32_t raw = angle_to_duty(angle);
    ledc_channel_config_t cc = {
        .gpio_num = gpio, .speed_mode = SERVO_MODE, .channel = ch,
        .timer_sel = SERVO_TIMER, .duty = raw, .hpoint = 0, .intr_type = LEDC_INTR_DISABLE,
    };
    if (ledc_channel_config(&cc) != ESP_OK) return IOT_CODE_HW;
    ledc_set_duty(SERVO_MODE, ch, raw);
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
