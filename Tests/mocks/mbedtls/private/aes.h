#pragma once
// Redirector for host tests built against system mbedtls (Debian gcc:12 ships
// mbedtls 2.28). Production firmware on ESP-IDF 6.0 uses mbedtls 4.0 where
// legacy crypto headers moved into mbedtls/private/. The system 2.28 still
// exposes them at the top level — just include the upstream header.
#include <mbedtls/aes.h>
