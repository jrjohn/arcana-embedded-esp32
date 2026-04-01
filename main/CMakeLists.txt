idf_component_register(SRCS "app_main.cpp"
                            "AppContainer.cpp"
                            "CommandBridgeServiceImpl.cpp"
                            "TimerServiceImpl.cpp"
                            "DiagnosticServiceImpl.cpp"
                    PRIV_REQUIRES mqtt nvs_flash esp_netif esp_timer app_update spi_flash ObservableSensor BleService CommandService RgbLed OledDisplay MqttService AtsStorageService IoService OtaService RegistrationService HttpUploadService WifiService
                    INCLUDE_DIRS ".")
