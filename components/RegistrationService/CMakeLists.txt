idf_component_register(
    SRCS "RegistrationServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES AtsStorageService CommandService ArcanaTs esp_http_client esp-tls mbedtls freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
