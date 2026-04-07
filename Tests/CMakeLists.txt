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

# Common includes — mocks FIRST so they override real ESP-IDF / FreeRTOS headers
set(COMMON_INCS
    ${MOCKS_DIR}
    ${CMD_INC}
    ${OBS_INC}
)

# Stubs library (FreeRTOS + esp_log + esp_timer)
add_library(esp_stubs STATIC ${MOCKS_DIR}/esp_stubs.cpp)
target_include_directories(esp_stubs PUBLIC ${MOCKS_DIR})

# ── test_frame_codec ──────────────────────────────────────────────────────────
add_executable(test_frame_codec test_frame_codec.cpp)
target_include_directories(test_frame_codec PRIVATE ${COMMON_INCS})
target_link_libraries(test_frame_codec PRIVATE GTest::gtest_main esp_stubs)

# ── test_observable ───────────────────────────────────────────────────────────
add_executable(test_observable test_observable.cpp)
target_include_directories(test_observable PRIVATE ${COMMON_INCS})
target_link_libraries(test_observable PRIVATE GTest::gtest_main esp_stubs)

# ── CTest registration ────────────────────────────────────────────────────────
enable_testing()
add_test(NAME test_frame_codec COMMAND test_frame_codec)
add_test(NAME test_observable  COMMAND test_observable)
