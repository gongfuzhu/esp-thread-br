idf_component_register(SRCS "iot_cap_adc_read.c"
                    INCLUDE_DIRS "."
                    REQUIRES iot_device_core
                    PRIV_REQUIRES espressif__cjson esp_adc)