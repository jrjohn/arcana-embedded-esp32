idf_component_register(
    SRCS "src/ArcanaTsDb.cpp" "src/VfsFilePort.cpp"
    INCLUDE_DIRS "include" "include/ats"
    REQUIRES fatfs freertos mbedtls
)
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
