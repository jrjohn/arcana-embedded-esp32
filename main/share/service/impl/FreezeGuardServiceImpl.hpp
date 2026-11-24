#pragma once

#include "FreezeGuardService.hpp"
#include <atomic>

namespace Arcana::Freeze {

class FreezeGuardServiceImpl : public FreezeGuardService {
public:
    static FreezeGuardService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    esp_err_t watchCurrentTask() override;
    void feed(Stage stage) override;
    void pause(const char* reason) override;
    void resume() override;
    int watchdogTimeoutS() const override;

private:
    FreezeGuardServiceImpl() = default;
    ~FreezeGuardServiceImpl() override = default;
    FreezeGuardServiceImpl(const FreezeGuardServiceImpl&) = delete;
    FreezeGuardServiceImpl& operator=(const FreezeGuardServiceImpl&) = delete;

    std::atomic<bool> mWatching{false};
};

} // namespace Arcana::Freeze
