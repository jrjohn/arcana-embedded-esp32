# SdFat (greiman) as an ESP-IDF component, built in NON-Arduino mode with an
# external SPI driver (SPI_DRIVER_SELECT=3). The port/ shim provides the few
# Arduino symbols SdFat still calls (millis/digitalWrite/pinMode/...).
file(GLOB_RECURSE SDFAT_SRCS "src/*.cpp")
list(APPEND SDFAT_SRCS "port/arduino_compat.cpp")

idf_component_register(
    SRCS ${SDFAT_SRCS}
    INCLUDE_DIRS
        "src"
        "src/common"
        "src/ExFatLib"
        "src/FatLib"
        "src/FsLib"
        "src/SdCard"
        "src/SdCard/SdSpiCard"
        "src/SdCard/SdSpiCard/SpiDriver"
        "port"
    REQUIRES esp_driver_spi esp_driver_gpio esp_timer esp_rom driver freertos
)

# Non-Arduino build + external SPI driver. Force-include the compat shim so the
# Arduino symbols SdFat references are declared in every TU.
target_compile_definitions(${COMPONENT_LIB} PUBLIC
    ENABLE_ARDUINO_FEATURES=0
    ENABLE_ARDUINO_SERIAL=0
    ENABLE_ARDUINO_STRING=0
    SPI_DRIVER_SELECT=3
    SDFAT_FILE_TYPE=3
    USE_BLOCK_DEVICE_INTERFACE=1
    USE_EXFAT_DUAL_FAT=1     # power-fail-safe dual-FAT exFAT (dual-FAT + atomic ActiveFat commit)
)
target_compile_options(${COMPONENT_LIB} PRIVATE
    "SHELL:-include ${CMAKE_CURRENT_SOURCE_DIR}/port/arduino_compat.h"
    -Wno-error=unused-parameter -Wno-error=unused-variable -Wno-error=sign-compare
    -Wno-error=class-memaccess -Wno-error=maybe-uninitialized)
