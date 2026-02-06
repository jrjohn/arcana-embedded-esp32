idf_component_register(
    SRCS "src/BleService.cpp"
         "src/BleGap.cpp"
         "src/BleGattServer.cpp"
         "src/BleGattClient.cpp"
    INCLUDE_DIRS "include"
    REQUIRES bt nvs_flash esp_event ObservableSensor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
