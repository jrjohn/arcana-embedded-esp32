idf_component_register(
    SRCS "OtaServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_http_client freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
