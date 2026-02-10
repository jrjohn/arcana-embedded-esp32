idf_component_register(
    SRCS "ObservableSensor.cpp" "DhtSensor.cpp" "SensorServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_event freertos esp_timer driver
)

# Enable C++17 features
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
