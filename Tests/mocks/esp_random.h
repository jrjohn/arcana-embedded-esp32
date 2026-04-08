#pragma once
// Host-test stub for ESP-IDF's esp_random.h.
//
// Production firmware uses esp_fill_random() (backed by ESP32 hardware TRNG)
// via the EspRng wrapper to satisfy mbedtls's f_rng callback signature.
// In host tests we don't have hardware RNG; provide a deterministic stub
// backed by libc rand() so tests are reproducible. impl in esp_stubs.cpp.

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

uint32_t esp_random(void);
void esp_fill_random(void* buf, size_t len);

#ifdef __cplusplus
}
#endif
