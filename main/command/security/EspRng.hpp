#pragma once

// mbedtls f_rng callback backed by ESP32 hardware RNG (esp_fill_random).
//
// Background: In mbedtls 4.0 (ESP-IDF 6.0+) the entire mbedtls_entropy_* /
// mbedtls_ctr_drbg_* API is gone — PSA Crypto owns randomness generation.
// The remaining low-level mbedtls_ecp_*/mbedtls_ecdh_* functions still expect
// a function pointer with the legacy f_rng signature though, so we provide a
// thin wrapper that delegates to esp_fill_random() (which is itself backed by
// the same hardware TRNG that PSA uses internally).

#include "esp_random.h"
#include <cstddef>

namespace Arcana {
namespace Crypto {

// Matches mbedtls's `int (*f_rng)(void *p_rng, unsigned char *output, size_t output_len)`.
// p_rng is unused — esp_fill_random reads from hardware directly.
inline int EspRngCallback(void* /*p_rng*/, unsigned char* output, size_t output_len) {
    esp_fill_random(output, output_len);
    return 0;
}

} // namespace Crypto
} // namespace Arcana
