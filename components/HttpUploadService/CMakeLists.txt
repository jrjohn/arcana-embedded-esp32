idf_component_register(
    SRCS "HttpUploadServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES AtsStorageService RegistrationService IoService esp_http_client freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
