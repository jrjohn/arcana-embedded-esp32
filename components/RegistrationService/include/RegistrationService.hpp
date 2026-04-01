#pragma once

#include "esp_err.h"
#include <cstdint>

namespace Arcana::Registration {

/**
 * Device registration service — TOFU provisioning.
 * First boot: POST /api/register → get MQTT credentials + comm_key.
 * Subsequent boots: load stored credentials from device.ats.
 */
class RegistrationService {
public:
    struct Credentials {
        char mqttUser[36]    = {};
        char mqttPass[36]    = {};
        char mqttBroker[36]  = {};
        uint16_t mqttPort    = 0;
        char uploadToken[72] = {};
        char topicPrefix[36] = {};
        uint8_t commKey[32]  = {};
        bool hasCommKey      = false;
        bool valid           = false;
    };

    virtual ~RegistrationService() = default;

    virtual bool isRegistered() const = 0;
    virtual const Credentials& credentials() const = 0;

    /// Attempt registration via HTTP POST. Call after WiFi is connected.
    virtual bool doRegistration() = 0;

    /// Load credentials from device.ats. Call at boot.
    virtual bool loadCredentials() = 0;

    /// Get device ID (MAC-based hex string)
    virtual const char* deviceId() const = 0;

protected:
    RegistrationService() = default;
};

} // namespace Arcana::Registration
