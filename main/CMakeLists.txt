# Per-target board directory: exactly one BoardConfig.hpp + Board.cpp
if(CONFIG_IDF_TARGET_ESP32S3)
    set(BOARD_DIR "esp32s3")
    set(BOARD_SRCS "esp32s3/Board.cpp" "esp32s3/St7789Lcd.cpp" "esp32s3/Xl9555.cpp" "esp32s3/TsensSensor.cpp")
else()
    set(BOARD_DIR "esp32")
    set(BOARD_SRCS "esp32/Board.cpp")
endif()

idf_component_register(
    SRCS
        # Entry + DI
        "App.cpp"
        ${BOARD_SRCS}
        "share/AppContainer.cpp"

        # service/impl/
        "share/service/impl/AtsStorageServiceImpl.cpp"
        "share/service/impl/CommandBridgeServiceImpl.cpp"
        "share/service/impl/CommandServiceImpl.cpp"
        "share/service/impl/DiagnosticServiceImpl.cpp"
        "share/service/impl/DriverServiceImpl.cpp"
        "share/service/impl/HttpUploadServiceImpl.cpp"
        "share/service/impl/IoServiceImpl.cpp"
        "share/service/impl/LcdServiceImpl.cpp"
        "share/service/impl/LedServiceImpl.cpp"
        "share/service/impl/OtaServiceImpl.cpp"
        "share/service/impl/RegistrationServiceImpl.cpp"
        "share/service/impl/SdBenchmarkServiceImpl.cpp"
        "share/service/impl/SensorServiceImpl.cpp"
        "share/service/impl/TimerServiceImpl.cpp"

        # transport/ble/
        "share/transport/ble/BleService.cpp"
        "share/transport/ble/BleGap.cpp"
        "share/transport/ble/BleGattServer.cpp"
        "share/transport/ble/BleGattClient.cpp"
        "share/transport/ble/BleTransportServiceImpl.cpp"

        # transport/mqtt/
        "share/transport/mqtt/MqttTransportServiceImpl.cpp"

        # transport/wifi/
        "share/transport/wifi/WifiServiceImpl.cpp"

        # db/arcanats/
        "share/db/arcanats/ats/ArcanaTsDb.cpp"
        "share/db/arcanats/port/VfsFilePort.cpp"

        # command/
        "share/command/CommandFactory.cpp"
        "share/command/CommandDispatcher.cpp"
        "share/command/codec/CommandCodec.cpp"
        "share/command/codec/arcana_cmd.pb.c"
        "share/command/security/CryptoEngine.cpp"
        "share/command/security/KeyExchangeManager.cpp"

        # driver/
        "share/driver/Ssd1306.cpp"
        "share/driver/RgbLed.cpp"
        "share/driver/DhtSensor.cpp"

        # core/
        "share/core/event/ObservableSensor.cpp"
        "share/core/log/log_dummy.c"

    INCLUDE_DIRS
        "."
        "${BOARD_DIR}"
        "share"
        "share/service"
        "share/service/impl"
        "share/transport/ble"
        "share/transport/ble/impl"
        "share/transport/mqtt"
        "share/transport/mqtt/impl"
        "share/transport/wifi"
        "share/transport/wifi/impl"
        "share/db/arcanats"
        "share/db/arcanats/ats"
        "share/db/arcanats/security"
        "share/db/arcanats/port"
        "share/command"
        "share/command/commands"
        "share/command/codec"
        "share/command/security"
        "share/view"
        "share/view/main"
        "share/driver"
        "share/core"
        "share/core/event"
        "share/core/log"
        "share/core/model"

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
        esp_driver_i2c
        esp_driver_spi
        esp_driver_tsens
        esp_driver_rmt
        esp_driver_gpio
        log
        # storage / FS
        fatfs
        sdmmc
        # network / IPC
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
