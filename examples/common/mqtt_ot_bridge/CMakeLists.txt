idf_component_register(SRC_DIRS "src"
                       INCLUDE_DIRS "include"
                       PRIV_INCLUDE_DIRS "src"
                       REQUIRES openthread
                       PRIV_REQUIRES esp_netif freertos esp_event espressif__mqtt espressif__cjson)
