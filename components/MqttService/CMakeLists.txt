idf_component_register(
    SRCS "MqttTransportServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES mqtt ObservableSensor esp_event
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
