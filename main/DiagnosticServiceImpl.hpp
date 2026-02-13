#pragma once

#include "DiagnosticService.hpp"
#include <atomic>

namespace Arcana {
namespace Diagnostic {

class DiagnosticServiceImpl : public DiagnosticService {
public:
    static DiagnosticService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

private:
    DiagnosticServiceImpl() = default;
    ~DiagnosticServiceImpl() override = default;
    DiagnosticServiceImpl(const DiagnosticServiceImpl&) = delete;
    DiagnosticServiceImpl& operator=(const DiagnosticServiceImpl&) = delete;

    std::atomic<bool> mRunning{false};
    uint8_t mTickCount = 0;
};

} // namespace Diagnostic
} // namespace Arcana
