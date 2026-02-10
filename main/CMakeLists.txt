idf_component_register(SRCS "app_main.cpp"
                            "Controller.cpp"
                            "CommandBridgeServiceImpl.cpp"
                            "TimerServiceImpl.cpp"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif esp_timer ObservableSensor BleService CommandService RgbLed OledDisplay MqttService
                    INCLUDE_DIRS ".")
