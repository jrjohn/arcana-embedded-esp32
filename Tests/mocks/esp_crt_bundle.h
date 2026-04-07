#pragma once
// Stub esp_crt_bundle.h for unit tests.
#include "esp_err.h"
#ifdef __cplusplus
extern "C" {
#endif

// Production: registers the global X.509 cert bundle for an mbedtls SSL config.
// Stub: no-op (tests don't perform real TLS).
struct mbedtls_ssl_config;
inline esp_err_t esp_crt_bundle_attach(void*) { return ESP_OK; }

#ifdef __cplusplus
}
#endif
