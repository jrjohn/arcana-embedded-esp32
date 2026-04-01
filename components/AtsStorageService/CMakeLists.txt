idf_component_register(
    SRCS "AtsStorageServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES ArcanaTs ObservableSensor IoService freertos esp_timer driver fatfs sdmmc nvs_flash
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
