#pragma once

#include <cstdint>

namespace Arcana {

/**
 * OTA firmware update service interface.
 *
 * ESP32 implementation uses native esp_ota_ops + esp_http_client
 * to download and flash firmware directly (no SD card staging).
 */
class OtaService {
public:
    virtual ~OtaService() = default;

    /**
     * Start OTA update (blocks until complete or failed).
     * @param host  Server hostname/IP
     * @param port  Server port
     * @param path  HTTP path (e.g. "/firmware.bin")
     * @param expectedSize  Expected firmware size in bytes
     * @param expectedCrc32 Expected CRC-32 (IEEE) of firmware
     * @return true if succeeded and system will reset
     */
    virtual bool startUpdate(const char* host, uint16_t port,
                             const char* path, uint32_t expectedSize,
                             uint32_t expectedCrc32) = 0;

    virtual uint8_t getProgress() const = 0;
    virtual bool isActive() const = 0;
};

} // namespace Arcana
