idf_component_register(
    SRCS "src/CommandService.cpp"
         "src/CommandFactory.cpp"
         "src/CommandDispatcher.cpp"
         "src/CommandCodec.cpp"
         "src/CryptoEngine.cpp"
         "src/arcana_cmd.pb.c"
    INCLUDE_DIRS "include"
    REQUIRES mbedtls BleService ObservableSensor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
