#pragma once

#include "OtaService.hpp"

namespace Arcana {

class OtaServiceImpl : public OtaService {
public:
    static OtaServiceImpl& getInstance();

    bool startUpdate(const char* host, uint16_t port,
                     const char* path, uint32_t expectedSize,
                     uint32_t expectedCrc32) override;

    uint8_t getProgress() const override { return mProgress; }
    bool isActive() const override { return mActive; }

private:
    OtaServiceImpl() = default;

    volatile uint8_t mProgress = 0;
    volatile bool mActive = false;
};

} // namespace Arcana
