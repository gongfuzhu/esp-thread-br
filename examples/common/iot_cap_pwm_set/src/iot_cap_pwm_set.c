#include "iot_cap_pwm_set.h"
#include "iot_device_core.h"
#include "esp_log.h"
#include "driver/ledc.h"

#define TAG "iot_cap_pwm_set"
#define PWM_MODE       LEDC_LOW_SPEED_MODE
#define PWM_RES        LEDC_TIMER_10_BIT       // 0..1023
#define PWM_MAX_DUTY   1023
#define PWM_TIMER      LEDC_TIMER_0

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

static int pwm_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jgpio = cJSON_GetObjectItem(data, "gpio");
    cJSON *jfreq = cJSON_GetObjectItem(data, "freq");
    cJSON *jduty = cJSON_GetObjectItem(data, "duty");
    if (!cJSON_IsNumber(jgpio) || !cJSON_IsNumber(jfreq) || !cJSON_IsNumber(jduty)) {
        return IOT_CODE_PARAM;
    }
    int gpio = jgpio->valueint, freq = jfreq->valueint, duty = jduty->valueint;
    if (freq <= 0 || duty < 0 || duty > 100) {
        return IOT_CODE_PARAM;
    }

    if (!s_timer_ready) {
        ledc_timer_config_t tc = {
            .speed_mode = PWM_MODE, .duty_resolution = PWM_RES,
            .timer_num = PWM_TIMER, .freq_hz = freq, .clk_cfg = LEDC_AUTO_CLK,
        };
        if (ledc_timer_config(&tc) != ESP_OK) return IOT_CODE_HW;
        s_timer_ready = true;
    } else {
        ledc_set_freq(PWM_MODE, PWM_TIMER, freq);
    }

    int ch = alloc_channel(gpio);
    if (ch < 0) return IOT_CODE_HW;   // 通道耗尽

    uint32_t raw = (uint32_t)((duty * PWM_MAX_DUTY) / 100);
    ledc_channel_config_t cc = {
        .gpio_num = gpio, .speed_mode = PWM_MODE, .channel = ch,
        .timer_sel = PWM_TIMER, .duty = raw, .hpoint = 0, .intr_type = LEDC_INTR_DISABLE,
    };
    if (ledc_channel_config(&cc) != ESP_OK) return IOT_CODE_HW;
    ledc_set_duty(PWM_MODE, ch, raw);
    ledc_update_duty(PWM_MODE, ch);

    cJSON_AddNumberToObject(resp_data, "gpio", gpio);
    cJSON_AddNumberToObject(resp_data, "freq", freq);
    cJSON_AddNumberToObject(resp_data, "duty", duty);
    return IOT_CODE_OK;
}

void iot_cap_pwm_set_init(void) {
    if (!iot_device_register_handler("pwm_set", pwm_handler)) {
        ESP_LOGE(TAG, "register pwm_set failed");
    }
}
