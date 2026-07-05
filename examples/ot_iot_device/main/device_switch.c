#include "device_switch.h"
#include "led_strip.h"
#include "esp_err.h"
#include "esp_log.h"
#include "sdkconfig.h"

#define SWITCH_GPIO CONFIG_IOT_DEVICE_SWITCH_GPIO
#define TAG "device_switch"

// on 时的白色中低亮度(WS2812 很亮,取 16/255 够看且不刺眼)。
#define ON_R 16
#define ON_G 16
#define ON_B 16

static bool s_state = false;
static led_strip_handle_t s_strip = NULL;

void device_switch_init(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = SWITCH_GPIO,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    esp_err_t err = led_strip_new_rmt_device(&strip_config, &rmt_config, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %d", err);
        s_strip = NULL;
        return;
    }
    led_strip_clear(s_strip);
    s_state = false;
}

void device_switch_set(bool on) {
    s_state = on;
    if (s_strip == NULL) {
        return;
    }
    if (on) {
        led_strip_set_pixel(s_strip, 0, ON_R, ON_G, ON_B);
        led_strip_refresh(s_strip);
    } else {
        led_strip_clear(s_strip);
    }
}

bool device_switch_get(void) {
    return s_state;
}
