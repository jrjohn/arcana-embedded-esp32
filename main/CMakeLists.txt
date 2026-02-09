idf_component_register(SRCS "app_main.cpp"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif ObservableSensor BleService CommandService
                    INCLUDE_DIRS ".")
