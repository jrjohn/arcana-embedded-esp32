idf_component_register(
    SRCS "src/CommandService.cpp"
         "src/CommandFactory.cpp"
         "src/CommandDispatcher.cpp"
         "src/BleCommandAdapter.cpp"
         "src/MqttCommandAdapter.cpp"
    INCLUDE_DIRS "include"
    REQUIRES json BleService ObservableSensor
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
