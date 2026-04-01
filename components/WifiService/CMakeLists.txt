idf_component_register(
    SRCS "WifiServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES esp_wifi esp_netif esp_event freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)

# protocol_examples_common include path
target_include_directories(${COMPONENT_LIB} PRIVATE
    ${IDF_PATH}/examples/common_components/protocol_examples_common/include
)
