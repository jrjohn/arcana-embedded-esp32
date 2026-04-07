cmake_minimum_required(VERSION 3.14)
project(arcana_esp32_tests CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Coverage flags ────────────────────────────────────────────────────────────
add_compile_options(-O0 -g --coverage -fprofile-arcs -ftest-coverage -Wno-unused-parameter)
add_link_options(--coverage)

# ── FetchContent: Google Test ─────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Paths ─────────────────────────────────────────────────────────────────────
set(PROJECT_ROOT  ${CMAKE_SOURCE_DIR}/..)
set(MOCKS_DIR     ${CMAKE_SOURCE_DIR}/mocks)

set(CMD_INC       ${PROJECT_ROOT}/components/CommandService/include)
set(CMD_SRC       ${PROJECT_ROOT}/components/CommandService/src)
set(OBS_INC       ${PROJECT_ROOT}/components/ObservableSensor/include)
set(ATS_INC       ${PROJECT_ROOT}/components/ArcanaTs/include)
set(ATS_SRC       ${PROJECT_ROOT}/components/ArcanaTs/src)

# Common includes — mocks FIRST so they override real ESP-IDF / FreeRTOS headers
set(COMMON_INCS
    ${MOCKS_DIR}
    ${CMD_INC}
    ${OBS_INC}
    ${ATS_INC}
)

# Stubs library (FreeRTOS + esp_log + esp_timer)
add_library(esp_stubs STATIC ${MOCKS_DIR}/esp_stubs.cpp)
target_include_directories(esp_stubs PUBLIC ${MOCKS_DIR})

# Try to find system mbedtls early — needed by multiple test targets that
# transitively include CryptoEngine.hpp via KeyExchangeCommand etc.
find_library(MBEDCRYPTO_LIB NAMES mbedcrypto)
find_library(MBEDTLS_LIB    NAMES mbedtls)
find_path(MBEDTLS_INCLUDE_DIR mbedtls/ccm.h)

# ── test_frame_codec ──────────────────────────────────────────────────────────
add_executable(test_frame_codec test_frame_codec.cpp)
target_include_directories(test_frame_codec PRIVATE ${COMMON_INCS})
target_link_libraries(test_frame_codec PRIVATE GTest::gtest_main esp_stubs)

# ── test_observable ───────────────────────────────────────────────────────────
add_executable(test_observable test_observable.cpp)
target_include_directories(test_observable PRIVATE ${COMMON_INCS})
target_link_libraries(test_observable PRIVATE GTest::gtest_main esp_stubs)

# ── test_sensor_types ─────────────────────────────────────────────────────────
add_executable(test_sensor_types test_sensor_types.cpp)
target_include_directories(test_sensor_types PRIVATE ${COMMON_INCS})
target_link_libraries(test_sensor_types PRIVATE GTest::gtest_main esp_stubs)

# ── test_commands (Ping + GetDeviceInfo + GetMqttStatus) ─────────────────────
add_executable(test_commands test_commands.cpp)
target_include_directories(test_commands PRIVATE ${COMMON_INCS})
target_link_libraries(test_commands PRIVATE GTest::gtest_main esp_stubs)

# ── test_chacha20 (pure C++ stream cipher, no mbedtls) ───────────────────────
add_executable(test_chacha20 test_chacha20.cpp)
target_include_directories(test_chacha20 PRIVATE ${COMMON_INCS})
target_link_libraries(test_chacha20 PRIVATE GTest::gtest_main)

# ── test_ats_schema (header-only schema builder + CRC32) ─────────────────────
add_executable(test_ats_schema test_ats_schema.cpp)
target_include_directories(test_ats_schema PRIVATE ${COMMON_INCS})
target_link_libraries(test_ats_schema PRIVATE GTest::gtest_main)

# ── test_vfs_file_port (POSIX file wrapper, /tmp as mount point) ─────────────
add_executable(test_vfs_file_port
    test_vfs_file_port.cpp
    ${ATS_SRC}/VfsFilePort.cpp
)
target_include_directories(test_vfs_file_port PRIVATE ${COMMON_INCS})
target_link_libraries(test_vfs_file_port PRIVATE GTest::gtest_main)

# ── test_lcd_viewmodel (header-only MVVM ViewModel) ──────────────────────────
add_executable(test_lcd_viewmodel test_lcd_viewmodel.cpp)
target_include_directories(test_lcd_viewmodel PRIVATE
    ${COMMON_INCS}
    ${PROJECT_ROOT}/components/OledDisplay/include
)
target_link_libraries(test_lcd_viewmodel PRIVATE GTest::gtest_main esp_stubs)

# ── test_main_view (View + stub Ssd1306 for I2C-free testing) ────────────────
add_executable(test_main_view
    test_main_view.cpp
    mocks/Ssd1306_stub.cpp
)
target_include_directories(test_main_view PRIVATE
    ${COMMON_INCS}
    ${PROJECT_ROOT}/components/OledDisplay/include
)
target_link_libraries(test_main_view PRIVATE GTest::gtest_main esp_stubs)

# ── test_command_factory (links CommandFactory.cpp; needs mbedtls headers
#    via KeyExchangeCommand → KeyExchangeManager → CryptoEngine → mbedtls/ccm.h)
if(MBEDTLS_INCLUDE_DIR)
    add_executable(test_command_factory
        test_command_factory.cpp
        ${CMD_SRC}/CommandFactory.cpp
        mocks/sensor_kex_stubs.cpp
    )
    target_include_directories(test_command_factory PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
    )
    target_link_libraries(test_command_factory PRIVATE GTest::gtest_main esp_stubs)
    add_test(NAME test_command_factory COMMAND test_command_factory)

    # ── test_command_dispatcher (uses CommandFactory which needs mbedtls headers)
    add_executable(test_command_dispatcher
        test_command_dispatcher.cpp
        ${CMD_SRC}/CommandDispatcher.cpp
        ${CMD_SRC}/CommandFactory.cpp
        mocks/sensor_kex_stubs.cpp
    )
    target_include_directories(test_command_dispatcher PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
    )
    target_link_libraries(test_command_dispatcher PRIVATE GTest::gtest_main esp_stubs)
    add_test(NAME test_command_dispatcher COMMAND test_command_dispatcher)
else()
    message(WARNING "mbedtls not found — skipping test_command_factory and test_command_dispatcher (need mbedtls/ccm.h)")
endif()

# ── test_crypto_engine (AES-256-CCM, requires real mbedtls) ──────────────────
if(MBEDCRYPTO_LIB AND MBEDTLS_INCLUDE_DIR)
    message(STATUS "Building test_crypto_engine with mbedtls: ${MBEDCRYPTO_LIB}")
    add_executable(test_crypto_engine
        test_crypto_engine.cpp
        ${CMD_SRC}/CryptoEngine.cpp
    )
    # Use system include paths for real mbedtls (no mock override needed since
    # we removed Tests/mocks/mbedtls/)
    target_include_directories(test_crypto_engine PRIVATE
        ${MOCKS_DIR}
        ${CMD_INC}
        ${OBS_INC}
    )
    if(MBEDTLS_INCLUDE_DIR)
        target_include_directories(test_crypto_engine PRIVATE ${MBEDTLS_INCLUDE_DIR})
    endif()
    target_link_libraries(test_crypto_engine PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_crypto_engine COMMAND test_crypto_engine)
else()
    message(WARNING "mbedtls not found — skipping test_crypto_engine (install libmbedtls-dev)")
endif()

# ── CTest registration ────────────────────────────────────────────────────────
enable_testing()
add_test(NAME test_frame_codec  COMMAND test_frame_codec)
add_test(NAME test_observable   COMMAND test_observable)
add_test(NAME test_sensor_types COMMAND test_sensor_types)
add_test(NAME test_commands     COMMAND test_commands)
add_test(NAME test_chacha20     COMMAND test_chacha20)
add_test(NAME test_ats_schema     COMMAND test_ats_schema)
add_test(NAME test_vfs_file_port  COMMAND test_vfs_file_port)
add_test(NAME test_lcd_viewmodel  COMMAND test_lcd_viewmodel)
add_test(NAME test_main_view      COMMAND test_main_view)
