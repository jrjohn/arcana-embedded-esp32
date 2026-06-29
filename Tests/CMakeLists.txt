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

# Post-restructure: all production code lives in main/<layer>/.
# Tests use bare leaf #include "Foo.hpp" so each layer needs its own include path.
set(MAIN_DIR      ${PROJECT_ROOT}/main)

# command/ — interfaces, factory, dispatcher
set(CMD_INC       ${MAIN_DIR}/share/command)              # ICommand.hpp, CommandFactory.hpp, etc
set(CMD_SRC       ${MAIN_DIR}/share/command)              # CommandFactory.cpp, CommandDispatcher.cpp
set(CMD_CODEC     ${MAIN_DIR}/share/command/codec)        # CommandCodec.{hpp,cpp}, FrameCodec.hpp, arcana_cmd.pb.{h,c}
set(CMD_SECURITY  ${MAIN_DIR}/share/command/security)     # CryptoEngine.{hpp,cpp}, KeyExchangeManager.{hpp,cpp}

# core/ — Observable, Log, model types
set(SVC_INC       ${MAIN_DIR}/share/service)
set(CORE_DIR      ${MAIN_DIR}/share/core)
set(CORE_EVENT    ${MAIN_DIR}/share/core/event)
set(CORE_LOG      ${MAIN_DIR}/share/core/log)
set(CORE_MODEL    ${MAIN_DIR}/share/core/model)

# db/ — ArcanaTs
set(ATS_INC       ${MAIN_DIR}/share/db/arcanats)          # exposes ats/ subfolder for "ats/ICipher.hpp" includes
set(ATS_ATS       ${MAIN_DIR}/share/db/arcanats/ats)     # for ArcanaTsDb.hpp leaf include from .cpp
set(ATS_SECURITY  ${MAIN_DIR}/share/db/arcanats/security)
set(ATS_PORT      ${MAIN_DIR}/share/db/arcanats/port)

# transport/
set(BLE_INC       ${MAIN_DIR}/share/transport/ble)

# service/
set(SVC_DIR       ${MAIN_DIR}/share/service)
set(SVC_IMPL_DIR  ${MAIN_DIR}/share/service/impl)
set(REG_INC       ${SVC_DIR})
set(REG_SRC       ${SVC_IMPL_DIR})
set(IO_INC        ${SVC_DIR})
set(STORAGE_INC   ${SVC_DIR})
set(UPLOAD_INC    ${SVC_DIR})
set(UPLOAD_SRC    ${SVC_IMPL_DIR})

# view/ — BaseLcdView, MainView, MainViewModel
set(VIEW_DIR      ${MAIN_DIR}/share/view)
set(VIEW_MAIN_DIR ${MAIN_DIR}/share/view/main)

# driver/ — Ssd1306, RgbLed, DhtSensor (consumed by view tests)
set(DRIVER_DIR    ${MAIN_DIR}/share/driver)

set(NANOPB_DIR    ${PROJECT_ROOT}/managed_components/nikas-belogolov__nanopb)

# Common includes — mocks FIRST so they override real ESP-IDF / FreeRTOS headers.
# Includes core layers + command + db that virtually every test transitively needs.
set(COMMON_INCS
    ${MOCKS_DIR}
    ${CMD_INC}
    ${CMD_CODEC}
    ${CMD_SECURITY}
    ${SVC_INC}
    ${CORE_DIR}
    ${CORE_EVENT}
    ${CORE_LOG}
    ${CORE_MODEL}
    ${ATS_INC}
    ${ATS_ATS}
    ${ATS_SECURITY}
    ${ATS_PORT}
    ${SVC_DIR}
    ${SVC_IMPL_DIR}
    ${VIEW_DIR}
    ${VIEW_MAIN_DIR}
    ${DRIVER_DIR}
)

# Stubs library (FreeRTOS + esp_log + esp_timer)
add_library(esp_stubs STATIC ${MOCKS_DIR}/esp_stubs.cpp)
target_include_directories(esp_stubs PUBLIC ${MOCKS_DIR})

# ── SdFat host library (real ExFatLib on host) ───────────────────────────────
# Compiles the portable parts of vendored SdFat (ExFatLib + common) plus a host
# Arduino shim, so ExFatFilePort can be exercised against a real exFAT volume on
# the host. SdCard/FatLib/FsLib are NOT compiled — ExFatFilePort only uses
# ExFatVolume/ExFatFile (block I/O is a test-side FsBlockDeviceInterface).
set(SDFAT_DIR ${PROJECT_ROOT}/components/SdFat)
file(GLOB SDFAT_HOST_SRCS
    ${SDFAT_DIR}/src/ExFatLib/*.cpp
    ${SDFAT_DIR}/src/common/*.cpp
)
add_library(sdfat_host STATIC ${SDFAT_HOST_SRCS} ${CMAKE_SOURCE_DIR}/support/arduino_compat_host.cpp)
target_include_directories(sdfat_host PUBLIC
    ${SDFAT_DIR}/src
    ${SDFAT_DIR}/src/common
    ${SDFAT_DIR}/src/ExFatLib
    ${SDFAT_DIR}/src/FatLib
    ${SDFAT_DIR}/src/FsLib
    ${SDFAT_DIR}/src/SdCard
    ${SDFAT_DIR}/src/SdCard/SdSpiCard
    ${SDFAT_DIR}/src/SdCard/SdSpiCard/SpiDriver
    ${SDFAT_DIR}/port
)
target_compile_definitions(sdfat_host PUBLIC
    ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0 ENABLE_ARDUINO_STRING=0
    SPI_DRIVER_SELECT=3 SDFAT_FILE_TYPE=3 USE_BLOCK_DEVICE_INTERFACE=1
)
target_compile_options(sdfat_host PUBLIC
    "SHELL:-include ${SDFAT_DIR}/port/arduino_compat.h")
target_compile_options(sdfat_host PRIVATE -w)   # vendored code; don't fail host build on its warnings

# dual-FAT variant of the SdFat host lib (USE_EXFAT_DUAL_FAT=1) — for the power-fail
# repro/fuzz tooling. Same sources, dual-FAT/dual-bitmap + ActiveFat commit.
add_library(sdfat_host_dualfat STATIC ${SDFAT_HOST_SRCS} ${CMAKE_SOURCE_DIR}/support/arduino_compat_host.cpp)
target_include_directories(sdfat_host_dualfat PUBLIC
    ${SDFAT_DIR}/src ${SDFAT_DIR}/src/common ${SDFAT_DIR}/src/ExFatLib
    ${SDFAT_DIR}/src/FatLib ${SDFAT_DIR}/src/FsLib ${SDFAT_DIR}/src/SdCard
    ${SDFAT_DIR}/src/SdCard/SdSpiCard ${SDFAT_DIR}/src/SdCard/SdSpiCard/SpiDriver
    ${SDFAT_DIR}/port)
target_compile_definitions(sdfat_host_dualfat PUBLIC
    ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0 ENABLE_ARDUINO_STRING=0
    SPI_DRIVER_SELECT=3 SDFAT_FILE_TYPE=3 USE_BLOCK_DEVICE_INTERFACE=1
    USE_EXFAT_DUAL_FAT=1)
target_compile_options(sdfat_host_dualfat PUBLIC
    "SHELL:-include ${SDFAT_DIR}/port/arduino_compat.h")
target_compile_options(sdfat_host_dualfat PRIVATE -w)

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

# ── test_xl9555 (expander register-shadow logic, fake I2C) ───────────────────
add_executable(test_xl9555
    test_xl9555.cpp
    ${MAIN_DIR}/esp32s3/Xl9555.cpp
)
target_include_directories(test_xl9555 PRIVATE ${COMMON_INCS} ${MAIN_DIR}/esp32s3)
target_link_libraries(test_xl9555 PRIVATE GTest::gtest_main esp_stubs)

# ── test_tsens_sensor (S3 internal temperature sensor backend) ───────────────
add_executable(test_tsens_sensor
    test_tsens_sensor.cpp
    ${MAIN_DIR}/esp32s3/TsensSensor.cpp
    ${CORE_EVENT}/ObservableSensor.cpp
)
target_include_directories(test_tsens_sensor PRIVATE ${COMMON_INCS} ${MAIN_DIR}/esp32s3)
target_link_libraries(test_tsens_sensor PRIVATE GTest::gtest_main esp_stubs)

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
    ${ATS_PORT}/VfsFilePort.cpp
)
target_include_directories(test_vfs_file_port PRIVATE ${COMMON_INCS})
target_link_libraries(test_vfs_file_port PRIVATE GTest::gtest_main)

# ── format_image (host repro tool: SdFat exFAT format+write → .img for fsck_exfat)
add_executable(format_image
    support/format_image.cpp
    ${ATS_PORT}/ExFatFilePort.cpp
)
target_include_directories(format_image PRIVATE ${COMMON_INCS})
target_link_libraries(format_image PRIVATE sdfat_host_dualfat esp_stubs)

# ── format_fuzz (power-loss fuzz harness for the dual-FAT commit engine) ────────
add_executable(format_fuzz
    support/format_fuzz.cpp
    ${ATS_PORT}/ExFatFilePort.cpp
)
target_include_directories(format_fuzz PRIVATE ${COMMON_INCS})
target_link_libraries(format_fuzz PRIVATE sdfat_host_dualfat esp_stubs)

# ── remount_fuzz (validates the mount-time reseed / recovery path) ────────────
add_executable(remount_fuzz
    support/remount_fuzz.cpp
    ${ATS_PORT}/ExFatFilePort.cpp
)
target_include_directories(remount_fuzz PRIVATE ${COMMON_INCS})
target_link_libraries(remount_fuzz PRIVATE sdfat_host_dualfat esp_stubs)

# ── test_exfat_file_port (ExFatFilePort over real SdFat exFAT, sparse-file blkdev)
add_executable(test_exfat_file_port
    test_exfat_file_port.cpp
    ${ATS_PORT}/ExFatFilePort.cpp
)
target_include_directories(test_exfat_file_port PRIVATE ${COMMON_INCS})
target_link_libraries(test_exfat_file_port PRIVATE GTest::gtest_main sdfat_host esp_stubs)

# ── test_dualfat_reconcile (mount-time bitmap reconcile — power-fail safety) ──
# Links the USE_EXFAT_DUAL_FAT variant so reconcileBitmap/bitmapMarkUsed/reconcileDir
# are compiled in (sdfat_host_dualfat's PUBLIC defs + arduino_compat -include
# propagate to this target and the ExFatFilePort.cpp it compiles).
add_executable(test_dualfat_reconcile
    test_dualfat_reconcile.cpp
    ${ATS_PORT}/ExFatFilePort.cpp
)
target_include_directories(test_dualfat_reconcile PRIVATE ${COMMON_INCS})
target_link_libraries(test_dualfat_reconcile PRIVATE GTest::gtest_main sdfat_host_dualfat esp_stubs)
add_test(NAME test_dualfat_reconcile COMMAND test_dualfat_reconcile)

# ── test_log (Logger ring buffer + appenders, header-only) ───────────────────
add_executable(test_log test_log.cpp)
target_include_directories(test_log PRIVATE
    ${COMMON_INCS}
)
target_link_libraries(test_log PRIVATE GTest::gtest_main esp_stubs)

# ── test_arcana_ts_db (full TSDB with NullCipher + VfsFilePort + nop mutex) ─
add_executable(test_arcana_ts_db
    test_arcana_ts_db.cpp
    ${ATS_ATS}/ArcanaTsDb.cpp
    ${ATS_PORT}/VfsFilePort.cpp
)
target_include_directories(test_arcana_ts_db PRIVATE
    ${COMMON_INCS}
)
target_link_libraries(test_arcana_ts_db PRIVATE GTest::gtest_main esp_stubs)

# ── test_lcd_viewmodel (header-only MVVM ViewModel) ──────────────────────────
add_executable(test_lcd_viewmodel test_lcd_viewmodel.cpp)
target_include_directories(test_lcd_viewmodel PRIVATE
    ${COMMON_INCS}
)
target_link_libraries(test_lcd_viewmodel PRIVATE GTest::gtest_main esp_stubs)

# ── test_main_view (View + stub Ssd1306 for I2C-free testing) ────────────────
add_executable(test_main_view
    test_main_view.cpp
    mocks/Ssd1306_stub.cpp
)
target_include_directories(test_main_view PRIVATE
    ${COMMON_INCS}
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
        ${CMD_SECURITY}/CryptoEngine.cpp
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
        ${CMD_SECURITY}/CryptoEngine.cpp
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
            ${CMD_CODEC}/CommandCodec.cpp
            ${CMD_SECURITY}/CryptoEngine.cpp
            ${CMD_SECURITY}/KeyExchangeManager.cpp   # CommandCodec references kex symbols even in plaintext mode
            ${CMD_CODEC}/arcana_cmd.pb.c
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

        # ── test_command_codec_encrypted (same .cpp built with encryption define)
        # Re-uses CommandCodec.cpp + nanopb but compiles with the production
        # CONFIG_CMD_ENCRYPTION_ENABLED branch active to cover the encryption
        # paths in CommandCodec.cpp.
        add_executable(test_command_codec_encrypted
            test_command_codec_encrypted.cpp
            ${CMD_CODEC}/CommandCodec.cpp
            ${CMD_SECURITY}/CryptoEngine.cpp
            ${CMD_SECURITY}/KeyExchangeManager.cpp
            ${CMD_CODEC}/arcana_cmd.pb.c
            ${NANOPB_DIR}/pb_common.c
            ${NANOPB_DIR}/pb_encode.c
            ${NANOPB_DIR}/pb_decode.c
        )
        target_include_directories(test_command_codec_encrypted PRIVATE
            ${COMMON_INCS}
            ${MBEDTLS_INCLUDE_DIR}
            ${NANOPB_DIR}/include
        )
        target_compile_options(test_command_codec_encrypted PRIVATE
            ${MBEDTLS_COMPAT_FLAGS}
        )
        target_compile_definitions(test_command_codec_encrypted PRIVATE
            CONFIG_CMD_ENCRYPTION_ENABLED=1
            CONFIG_CMD_ENCRYPTION_PSK="000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"
        )
        target_link_libraries(test_command_codec_encrypted PRIVATE
            GTest::gtest_main
            esp_stubs
            ${MBEDCRYPTO_LIB}
        )
        add_test(NAME test_command_codec_encrypted COMMAND test_command_codec_encrypted)
    else()
        message(WARNING "nanopb not vendored — skipping test_command_codec")
    endif()

    # ── test_key_exchange_manager (ECDH P-256 + HKDF + HMAC, real mbedtls) ──
    add_executable(test_key_exchange_manager
        test_key_exchange_manager.cpp
        ${CMD_SECURITY}/KeyExchangeManager.cpp
        ${CMD_SECURITY}/CryptoEngine.cpp
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

    # ── test_kex_failure_injection: --wrap mbedtls fault injection ─────────
    # Drives the mbedtls failure paths in KeyExchangeManager via linker
    # symbol wrapping. Each call to mbedtls_md_setup etc. is rerouted to
    # __wrap_mbedtls_md_setup in mocks/mbedtls_wrap.cpp which can inject
    # failures via global flags. Linux/gold ld supports --wrap; macOS
    # ld does not, so this target is Linux-only (Docker CI).
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(test_kex_failure_injection
            test_kex_failure_injection.cpp
            mocks/mbedtls_wrap.cpp
            ${CMD_SECURITY}/KeyExchangeManager.cpp
            ${CMD_SECURITY}/CryptoEngine.cpp
            ${CMD_CODEC}/CommandCodec.cpp
            ${CMD_CODEC}/arcana_cmd.pb.c
            ${NANOPB_DIR}/pb_common.c
            ${NANOPB_DIR}/pb_encode.c
            ${NANOPB_DIR}/pb_decode.c
        )
        target_include_directories(test_kex_failure_injection PRIVATE
            ${COMMON_INCS}
            ${MBEDTLS_INCLUDE_DIR}
            ${NANOPB_DIR}/include
        )
        target_compile_definitions(test_kex_failure_injection PRIVATE
            CONFIG_CMD_ENCRYPTION_ENABLED=1
            CONFIG_CMD_ENCRYPTION_PSK="000102030405060708090A0B0C0D0E0F101112131415161718191A1B1C1D1E1F"
        )
        target_compile_options(test_kex_failure_injection PRIVATE ${MBEDTLS_COMPAT_FLAGS})
        target_link_options(test_kex_failure_injection PRIVATE
            -Wl,--wrap=mbedtls_md_setup
            -Wl,--wrap=mbedtls_md_hmac_starts
            -Wl,--wrap=mbedtls_md_hmac_update
            -Wl,--wrap=mbedtls_md_hmac_finish
            -Wl,--wrap=mbedtls_ecp_group_load
            -Wl,--wrap=mbedtls_ecp_gen_keypair
            -Wl,--wrap=mbedtls_ecp_check_pubkey
            -Wl,--wrap=mbedtls_ecdh_compute_shared
            -Wl,--wrap=mbedtls_mpi_read_binary
            -Wl,--wrap=mbedtls_mpi_write_binary
            -Wl,--wrap=mbedtls_ccm_setkey
            -Wl,--wrap=mbedtls_ccm_encrypt_and_tag
            -Wl,--wrap=mbedtls_ccm_auth_decrypt
        )
        target_link_libraries(test_kex_failure_injection PRIVATE
            GTest::gtest_main
            esp_stubs
            ${MBEDCRYPTO_LIB}
        )
        add_test(NAME test_kex_failure_injection COMMAND test_kex_failure_injection)
    endif()

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
        ${SVC_IMPL_DIR}/CommandServiceImpl.cpp
        ${CMD_SRC}/CommandDispatcher.cpp
        ${CMD_SRC}/CommandFactory.cpp
        ${CMD_SECURITY}/CryptoEngine.cpp
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
        ${CMD_SECURITY}/CryptoEngine.cpp
    )
    # Use system include paths for real mbedtls (no mock override needed since
    # we removed Tests/mocks/mbedtls/)
    target_include_directories(test_crypto_engine PRIVATE
        ${MOCKS_DIR}
        ${CMD_INC}
        ${CMD_SECURITY}
        ${CORE_EVENT}
        ${ATS_INC}        # exposes ats/ subfolder for "ats/ICipher.hpp"
        ${ATS_ATS}
        ${ATS_SECURITY}   # for Esp32AesCtrCipher.hpp
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
add_test(NAME test_xl9555       COMMAND test_xl9555)
add_test(NAME test_tsens_sensor COMMAND test_tsens_sensor)
add_test(NAME test_commands     COMMAND test_commands)
add_test(NAME test_chacha20     COMMAND test_chacha20)
add_test(NAME test_ats_schema     COMMAND test_ats_schema)
add_test(NAME test_vfs_file_port  COMMAND test_vfs_file_port)
add_test(NAME test_exfat_file_port COMMAND test_exfat_file_port)
add_test(NAME test_log            COMMAND test_log)
add_test(NAME test_arcana_ts_db   COMMAND test_arcana_ts_db)
add_test(NAME test_lcd_viewmodel  COMMAND test_lcd_viewmodel)
add_test(NAME test_main_view      COMMAND test_main_view)
