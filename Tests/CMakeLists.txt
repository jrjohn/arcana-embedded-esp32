cmake_minimum_required(VERSION 3.14)
project(arcana_esp32_tests C CXX)

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
set(LOG_INC       ${PROJECT_ROOT}/components/LogService/include)
set(BLE_INC       ${PROJECT_ROOT}/components/BleService/include)
set(REG_INC       ${PROJECT_ROOT}/components/RegistrationService/include)
set(REG_SRC       ${PROJECT_ROOT}/components/RegistrationService)
set(IO_INC        ${PROJECT_ROOT}/components/IoService/include)
set(STORAGE_INC   ${PROJECT_ROOT}/components/AtsStorageService/include)
set(UPLOAD_INC    ${PROJECT_ROOT}/components/HttpUploadService/include)
set(UPLOAD_SRC    ${PROJECT_ROOT}/components/HttpUploadService)
set(NANOPB_DIR    ${PROJECT_ROOT}/managed_components/nikas-belogolov__nanopb)

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

# Production code uses MBEDTLS_PRIVATE(field) accessors from mbedtls 3.x.
# Debian's gcc:12 base ships mbedtls 2.28 which exposes fields directly and
# lacks the macro. Force-include a compat shim before mbedtls headers.
set(MBEDTLS_COMPAT_FLAGS "-include${MOCKS_DIR}/mbedtls_compat.h")

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

# ── test_log (Logger ring buffer + appenders, header-only) ───────────────────
add_executable(test_log test_log.cpp)
target_include_directories(test_log PRIVATE
    ${COMMON_INCS}
    ${LOG_INC}
)
target_link_libraries(test_log PRIVATE GTest::gtest_main esp_stubs)

# ── test_arcana_ts_db (full TSDB with NullCipher + VfsFilePort + nop mutex) ─
add_executable(test_arcana_ts_db
    test_arcana_ts_db.cpp
    ${ATS_SRC}/ArcanaTsDb.cpp
    ${ATS_SRC}/VfsFilePort.cpp
)
target_include_directories(test_arcana_ts_db PRIVATE
    ${COMMON_INCS}
    ${ATS_INC}/ats   # ArcanaTsDb.cpp uses #include "ArcanaTsDb.hpp" without ats/ prefix
    ${LOG_INC}       # AtsAppender.hpp + EventCodes.hpp
)
target_link_libraries(test_arcana_ts_db PRIVATE GTest::gtest_main esp_stubs)

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
#    Also links CryptoEngine.cpp + mbedcrypto for ~CryptoEngine() symbol that
#    KeyExchangeManager::Session's implicit destructor pulls in.
if(MBEDTLS_INCLUDE_DIR AND MBEDCRYPTO_LIB)
    add_executable(test_command_factory
        test_command_factory.cpp
        ${CMD_SRC}/CommandFactory.cpp
        ${CMD_SRC}/CryptoEngine.cpp
        mocks/sensor_kex_stubs.cpp
    )
    target_include_directories(test_command_factory PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
        ${BLE_INC}
    )
    target_compile_options(test_command_factory PRIVATE ${MBEDTLS_COMPAT_FLAGS})
    target_link_libraries(test_command_factory PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_command_factory COMMAND test_command_factory)

    # ── test_command_dispatcher (uses CommandFactory which needs mbedtls headers)
    add_executable(test_command_dispatcher
        test_command_dispatcher.cpp
        ${CMD_SRC}/CommandDispatcher.cpp
        ${CMD_SRC}/CommandFactory.cpp
        ${CMD_SRC}/CryptoEngine.cpp
        mocks/sensor_kex_stubs.cpp
    )
    target_include_directories(test_command_dispatcher PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
    )
    target_compile_options(test_command_dispatcher PRIVATE ${MBEDTLS_COMPAT_FLAGS})
    target_link_libraries(test_command_dispatcher PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_command_dispatcher COMMAND test_command_dispatcher)

    # ── test_command_codec (protobuf + frame wrapping, plaintext mode) ─────
    if(EXISTS ${NANOPB_DIR}/pb_encode.c)
        add_executable(test_command_codec
            test_command_codec.cpp
            ${CMD_SRC}/CommandCodec.cpp
            ${CMD_SRC}/CryptoEngine.cpp
            ${CMD_SRC}/KeyExchangeManager.cpp   # CommandCodec references kex symbols even in plaintext mode
            ${CMD_SRC}/arcana_cmd.pb.c
            ${NANOPB_DIR}/pb_common.c
            ${NANOPB_DIR}/pb_encode.c
            ${NANOPB_DIR}/pb_decode.c
        )
        target_include_directories(test_command_codec PRIVATE
            ${COMMON_INCS}
            ${MBEDTLS_INCLUDE_DIR}
            ${NANOPB_DIR}/include
        )
        target_compile_options(test_command_codec PRIVATE ${MBEDTLS_COMPAT_FLAGS})
        target_link_libraries(test_command_codec PRIVATE
            GTest::gtest_main
            esp_stubs
            ${MBEDCRYPTO_LIB}
        )
        add_test(NAME test_command_codec COMMAND test_command_codec)
    else()
        message(WARNING "nanopb not vendored — skipping test_command_codec")
    endif()

    # ── test_key_exchange_manager (ECDH P-256 + HKDF + HMAC, real mbedtls) ──
    add_executable(test_key_exchange_manager
        test_key_exchange_manager.cpp
        ${CMD_SRC}/KeyExchangeManager.cpp
        ${CMD_SRC}/CryptoEngine.cpp
    )
    target_include_directories(test_key_exchange_manager PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
    )
    target_compile_options(test_key_exchange_manager PRIVATE ${MBEDTLS_COMPAT_FLAGS})
    target_link_libraries(test_key_exchange_manager PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_key_exchange_manager COMMAND test_key_exchange_manager)

    # ── test_registration_service (Phase D — needs http_client + storage stubs)
    add_executable(test_registration_service
        test_registration_service.cpp
        ${REG_SRC}/RegistrationServiceImpl.cpp
        mocks/esp_http_client_stub.cpp
    )
    target_include_directories(test_registration_service PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
        ${REG_INC}
        ${STORAGE_INC}
    )
    target_compile_options(test_registration_service PRIVATE ${MBEDTLS_COMPAT_FLAGS})
    target_link_libraries(test_registration_service PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_registration_service COMMAND test_registration_service)

    # ── test_http_upload_service (Phase D — uses real RegistrationServiceImpl)
    add_executable(test_http_upload_service
        test_http_upload_service.cpp
        ${UPLOAD_SRC}/HttpUploadServiceImpl.cpp
        ${REG_SRC}/RegistrationServiceImpl.cpp
        mocks/esp_http_client_stub.cpp
    )
    target_include_directories(test_http_upload_service PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
        ${REG_INC}
        ${STORAGE_INC}
        ${UPLOAD_INC}
        ${IO_INC}
    )
    target_compile_options(test_http_upload_service PRIVATE ${MBEDTLS_COMPAT_FLAGS})
    target_link_libraries(test_http_upload_service PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_http_upload_service COMMAND test_http_upload_service)

    # ── test_command_service (Meyer singleton lifecycle + request routing) ──
    add_executable(test_command_service
        test_command_service.cpp
        ${CMD_SRC}/CommandService.cpp
        ${CMD_SRC}/CommandDispatcher.cpp
        ${CMD_SRC}/CommandFactory.cpp
        ${CMD_SRC}/CryptoEngine.cpp
        mocks/sensor_kex_stubs.cpp
    )
    target_include_directories(test_command_service PRIVATE
        ${COMMON_INCS}
        ${MBEDTLS_INCLUDE_DIR}
    )
    target_compile_options(test_command_service PRIVATE ${MBEDTLS_COMPAT_FLAGS})
    target_link_libraries(test_command_service PRIVATE
        GTest::gtest_main
        esp_stubs
        ${MBEDCRYPTO_LIB}
    )
    add_test(NAME test_command_service COMMAND test_command_service)
else()
    message(WARNING "mbedtls/mbedcrypto not found — skipping test_command_factory and test_command_dispatcher")
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
    target_compile_options(test_crypto_engine PRIVATE ${MBEDTLS_COMPAT_FLAGS})
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
add_test(NAME test_log            COMMAND test_log)
add_test(NAME test_arcana_ts_db   COMMAND test_arcana_ts_db)
add_test(NAME test_lcd_viewmodel  COMMAND test_lcd_viewmodel)
add_test(NAME test_main_view      COMMAND test_main_view)
