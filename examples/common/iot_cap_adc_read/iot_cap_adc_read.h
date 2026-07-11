#ifndef IOT_CAP_ADC_READ_H
#define IOT_CAP_ADC_READ_H

#include "iot_device_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize ADC read capability component
 *
 * Registers the "adc_read" event handler with the IoT device core.
 *
 * @return ESP_OK on successful registration, error code otherwise
 */
esp_err_t iot_cap_adc_read_init(void);

#ifdef __cplusplus
}
#endif

#endif // IOT_CAP_ADC_READ_H