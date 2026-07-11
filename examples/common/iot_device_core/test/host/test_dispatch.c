#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include "cJSON.h"
#include "../../src/iot_dispatch.h"

static int h_switch(const cJSON *d, cJSON *r) { (void)d; (void)r; return 0; }
static int h_pwm(const cJSON *d, cJSON *r) { (void)d; (void)r; return 0; }

int main(void) {
    iot_dispatch_reset();

    assert(iot_dispatch_register("switch", h_switch));
    assert(iot_dispatch_register("pwm_set", h_pwm));
    // 重复注册被拒
    assert(!iot_dispatch_register("switch", h_switch));

    assert(iot_dispatch_lookup("switch") == h_switch);
    assert(iot_dispatch_lookup("pwm_set") == h_pwm);
    // 未注册返回 NULL
    assert(iot_dispatch_lookup("adc_read") == NULL);

    printf("test_dispatch OK\n");
    return 0;
}
