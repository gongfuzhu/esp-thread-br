#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "cJSON.h"
#include "iot_device_core.h"

static const char *TAG = "iot_cap_adc_read";
static adc_oneshot_unit_handle_t s_adc_unit = NULL;
static adc_cali_handle_t s_adc_cali = NULL;

static int adc_read_handler(const cJSON *input, cJSON *output)
{
    // Validate input parameters
    if (!input || !cJSON_IsObject(input)) {
        ESP_LOGE(TAG, "Invalid input JSON");
        return IOT_CODE_PARAM;
    }

    cJSON *channel_json = cJSON_GetObjectItem(input, "channel");
    if (!channel_json || !cJSON_IsNumber(channel_json)) {
        ESP_LOGE(TAG, "Missing or non-numeric 'channel' parameter");
        return IOT_CODE_PARAM;
    }

    int channel = channel_json->valueint;
    if (channel < 0 || channel >= SOC_ADC_CHANNEL_NUM) {
        ESP_LOGE(TAG, "ADC channel %d out of valid range (0-%d)", channel, SOC_ADC_CHANNEL_NUM - 1);
        return IOT_CODE_PARAM;
    }

    // Lazy initialize ADC unit
    if (!s_adc_unit) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_RTC_CLK_SRC_DEFAULT,
        };

        if (adc_oneshot_new_unit(&unit_cfg, &s_adc_unit) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize ADC oneshot unit");
            return IOT_CODE_HW;
        }
    }

    // Configure ADC channel
    adc_oneshot_chan_cfg_t chan_cfg = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };

    if (adc_oneshot_config_channel(s_adc_unit, channel, &chan_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC channel %d", channel);
        return IOT_CODE_HW;
    }

    // Lazy initialize calibration handle
    if (!s_adc_cali) {
        // Try curve fitting calibration scheme first
        if (adc_cali_create_scheme_curve_fitting(ADC_UNIT_1, &s_adc_cali) != ESP_OK) {
            // Fall back to linear calibration scheme
            ESP_LOGW(TAG, "Curve fitting calibration failed, using linear estimate");
            if (adc_cali_create_scheme_linear(ADC_UNIT_1, &s_adc_cali) != ESP_OK) {
                ESP_LOGW(TAG, "No calibration scheme available, will return raw values only");
                s_adc_cali = NULL;
            }
        }
    }

    // Read raw ADC value
    int raw_val = 0;
    if (adc_oneshot_read(s_adc_unit, channel, &raw_val) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read ADC channel %d", channel);
        return IOT_CODE_HW;
    }

    // Calculate voltage
    float voltage = 0.0f;
    if (s_adc_cali != NULL) {
        if (adc_cali_raw_to_voltage(s_adc_cali, raw_val, &voltage) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to calibrate raw value %d", raw_val);
            voltage = 0.0f;
        }
    } else {
        // Linear estimate: 3.3V reference, 12-bit resolution
        voltage = (raw_val * 3.3f) / 4095.0f;
    }

    // Populate response output
    if (!output || !cJSON_IsObject(output)) {
        ESP_LOGE(TAG, "Invalid output JSON");
        return IOT_CODE_PARAM;
    }

    cJSON_AddNumberToObject(output, "channel", channel);
    cJSON_AddNumberToObject(output, "raw_val", raw_val);
    cJSON_AddNumberToObject(output, "voltage", voltage);

    return IOT_CODE_OK;
}

esp_err_t iot_cap_adc_read_init(void)
{
    return iot_device_core_register_handler("adc_read", adc_read_handler);
}