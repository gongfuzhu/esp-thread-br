idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       REQUIRES iot_device_core
                       PRIV_REQUIRES espressif__cjson esp_driver_gpio freertos)