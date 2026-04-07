/*
 * Compatibility shim for system mbedtls in unit tests.
 *
 * Production firmware uses mbedtls 3.x bundled with ESP-IDF, where struct
 * fields are mangled and accessed via the MBEDTLS_PRIVATE(X) macro.
 *
 * Debian Bookworm (gcc:12 base image) ships mbedtls 2.28.x, which exposes
 * fields directly (Q.X, Q.Y, Q.Z) and lacks the MBEDTLS_PRIVATE macro
 * altogether.
 *
 * Force-include this header before any mbedtls usage to provide a no-op
 * MBEDTLS_PRIVATE on the 2.x system, so the same source compiles in both
 * environments.
 */
#pragma once

#ifndef MBEDTLS_PRIVATE
#define MBEDTLS_PRIVATE(member) member
#endif
