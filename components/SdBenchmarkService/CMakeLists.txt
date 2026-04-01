idf_component_register(
    SRCS "SdBenchmarkServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES freertos esp_timer
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
