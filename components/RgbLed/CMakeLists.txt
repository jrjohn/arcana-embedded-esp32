idf_component_register(
    SRCS "RgbLed.cpp" "LedServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos esp_timer ObservableSensor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
