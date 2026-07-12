#include "iot_cap_adc_read.h"
#include "iot_device_core.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#define TAG "iot_cap_adc_read"
#define ADC_UNIT      ADC_UNIT_1
#define ADC_ATTEN     ADC_ATTEN_DB_12

// 组件级 static 句柄，首次使用创建后复用。
static adc_oneshot_unit_handle_t s_adc = NULL;
static adc_cali_handle_t s_cali = NULL;
static bool s_cali_ok = false;

static bool ensure_adc(void) {
    if (s_adc != NULL) return true;
    adc_oneshot_unit_init_cfg_t ucfg = { .unit_id = ADC_UNIT };
    if (adc_oneshot_new_unit(&ucfg, &s_adc) != ESP_OK) { s_adc = NULL; return false; }

    adc_cali_curve_fitting_config_t ccfg = {
        .unit_id = ADC_UNIT, .atten = ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    s_cali_ok = (adc_cali_create_scheme_curve_fitting(&ccfg, &s_cali) == ESP_OK);
    return true;
}

static int adc_handler(const cJSON *data, cJSON *resp_data) {
    cJSON *jch = cJSON_GetObjectItem(data, "channel");
    if (!cJSON_IsNumber(jch)) return IOT_CODE_PARAM;
    int channel = jch->valueint;
    if (channel < 0 || channel >= SOC_ADC_CHANNEL_NUM(ADC_UNIT)) return IOT_CODE_PARAM;

    if (!ensure_adc()) return IOT_CODE_HW;

    adc_oneshot_chan_cfg_t chcfg = { .atten = ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT };
    if (adc_oneshot_config_channel(s_adc, channel, &chcfg) != ESP_OK) return IOT_CODE_HW;

    int raw = 0;
    if (adc_oneshot_read(s_adc, channel, &raw) != ESP_OK) return IOT_CODE_HW;

    double voltage;
    int mv = 0;
    if (s_cali_ok && adc_cali_raw_to_voltage(s_cali, raw, &mv) == ESP_OK) {
        voltage = mv / 1000.0;
    } else {
        voltage = (raw * 3.3) / 4095.0;   // 线性估算兜底
    }
    cJSON_AddNumberToObject(resp_data, "channel", channel);
    cJSON_AddNumberToObject(resp_data, "raw_val", raw);
    cJSON_AddNumberToObject(resp_data, "voltage", voltage);
    return IOT_CODE_OK;
}

void iot_cap_adc_read_init(void) {
    if (!iot_device_register_handler("adc_read", adc_handler)) {
        ESP_LOGE(TAG, "register adc_read failed");
    }
}