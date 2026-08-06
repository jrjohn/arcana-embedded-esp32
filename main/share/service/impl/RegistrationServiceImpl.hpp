#pragma once

#include "RegistrationService.hpp"

namespace Arcana::Registration {

class RegistrationServiceImpl : public RegistrationService {
public:
    static RegistrationServiceImpl& getInstance();

    bool isRegistered() const override { return mCreds.valid; }
    const Credentials& credentials() const override { return mCreds; }
    bool doRegistration() override;
    bool loadCredentials() override;
    bool refreshToken() override;
    const char* deviceId() const override { return mDeviceId; }

    bool saveCredentials();

private:
    RegistrationServiceImpl();

    bool httpRegister();
    bool parseResponse(const uint8_t* payload, uint16_t len);

    Credentials mCreds;
    char mDeviceId[13];     ///< MAC hex "AABBCCDDEEFF\0"
    uint8_t mDeviceKey[32]; ///< Derived from MAC

    // Server ECDH response (temporary, used between parseResponse and ECDH)
    uint8_t mServerPub[64];
    uint8_t mServerPubLen = 0;
};

} // namespace Arcana::Registration
