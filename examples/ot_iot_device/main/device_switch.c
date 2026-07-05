#include "device_switch.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#define SWITCH_GPIO CONFIG_IOT_DEVICE_SWITCH_GPIO

static bool s_state = false;

void device_switch_init(void) {
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << SWITCH_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);
    gpio_set_level(SWITCH_GPIO, 0);
    s_state = false;
}

void device_switch_set(bool on) {
    s_state = on;
    gpio_set_level(SWITCH_GPIO, on ? 1 : 0);
}

bool device_switch_get(void) {
    return s_state;
}
