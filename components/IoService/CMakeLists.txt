idf_component_register(
    SRCS "IoServiceImpl.cpp"
    INCLUDE_DIRS "include"
    REQUIRES driver freertos
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
