idf_component_register(
    SRCS
        # Entry + DI
        "App.cpp"
        "AppContainer.cpp"

        # service/impl/
        "service/impl/AtsStorageServiceImpl.cpp"
        "service/impl/CommandBridgeServiceImpl.cpp"
        "service/impl/CommandServiceImpl.cpp"
        "service/impl/DiagnosticServiceImpl.cpp"
        "service/impl/HttpUploadServiceImpl.cpp"
        "service/impl/IoServiceImpl.cpp"
        "service/impl/LcdServiceImpl.cpp"
        "service/impl/LedServiceImpl.cpp"
        "service/impl/OtaServiceImpl.cpp"
        "service/impl/RegistrationServiceImpl.cpp"
        "service/impl/SdBenchmarkServiceImpl.cpp"
        "service/impl/SensorServiceImpl.cpp"
        "service/impl/TimerServiceImpl.cpp"

        # transport/ble/
        "transport/ble/BleService.cpp"
        "transport/ble/BleGap.cpp"
        "transport/ble/BleGattServer.cpp"
        "transport/ble/BleGattClient.cpp"
        "transport/ble/BleTransportServiceImpl.cpp"

        # transport/mqtt/
        "transport/mqtt/MqttTransportServiceImpl.cpp"

        # transport/wifi/
        "transport/wifi/WifiServiceImpl.cpp"

        # db/arcanats/
        "db/arcanats/ats/ArcanaTsDb.cpp"
        "db/arcanats/port/VfsFilePort.cpp"

        # command/
        "command/CommandFactory.cpp"
        "command/CommandDispatcher.cpp"
        "command/codec/CommandCodec.cpp"
        "command/codec/arcana_cmd.pb.c"
        "command/security/CryptoEngine.cpp"
        "command/security/KeyExchangeManager.cpp"

        # driver/
        "driver/Ssd1306.cpp"
        "driver/RgbLed.cpp"
        "driver/DhtSensor.cpp"

        # core/
        "core/event/ObservableSensor.cpp"
        "core/log/log_dummy.c"

    INCLUDE_DIRS
        "."
        "service"
        "service/impl"
        "transport/ble"
        "transport/ble/impl"
        "transport/mqtt"
        "transport/mqtt/impl"
        "transport/wifi"
        "transport/wifi/impl"
        "db/arcanats"
        "db/arcanats/ats"
        "db/arcanats/security"
        "db/arcanats/port"
        "command"
        "command/commands"
        "command/codec"
        "command/security"
        "view"
        "view/main"
        "driver"
        "core/event"
        "core/log"
        "core/model"

    PRIV_REQUIRES
        # framework
        nvs_flash
        esp_netif
        esp_timer
        esp_event
        esp_hw_support
        spi_flash
        freertos
        driver
        log
        # storage / FS
        fatfs
        sdmmc
        # network / IPC
        mqtt
        bt
        esp_wifi
        esp_http_client
        esp-tls
        # OTA
        app_update
        # crypto
        mbedtls
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# protocol_examples_common include path (was in WifiService component)
target_include_directories(${COMPONENT_LIB} PRIVATE
    ${IDF_PATH}/examples/common_components/protocol_examples_common/include
)
