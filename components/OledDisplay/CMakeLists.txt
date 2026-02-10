idf_component_register(
    SRCS "Ssd1306.cpp" "LcdServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos ObservableSensor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
