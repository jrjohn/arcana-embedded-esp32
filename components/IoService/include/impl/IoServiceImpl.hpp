#pragma once

#include "IoService.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdint>

namespace Arcana::Io {

/**
 * GPIO button service — polls buttons every 100ms.
 *
 * Button A (GPIO5, active-LOW):  press+release → upload, during upload → cancel
 * Button B (GPIO36, active-LOW): hold 2s → format SD
 */
class IoServiceImpl : public IoService {
public:
    static IoServiceImpl& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;

    bool isUploadRequested() const override { return mUploadRequested; }
    void clearUploadRequest() override      { mUploadRequested = false; }

    bool isCancelRequested() const override { return mCancelRequested; }
    void clearCancelRequest() override      { mCancelRequested = false; }

    bool isFormatRequested() const override { return mFormatRequested; }
    void clearFormatRequest() override      { mFormatRequested = false; }

    void armCancel() override;
    void disarmCancel() override;

private:
    IoServiceImpl();

    static void taskFunc(void* param);
    void taskLoop();

    volatile bool mUploadRequested = false;
    volatile bool mCancelRequested = false;
    volatile bool mFormatRequested = false;
    volatile bool mCancelArmed = false;
    uint32_t mCooldownUntil = 0;

    // Button A state
    bool mBtnASeen = false;
    bool mBtnAPrev = false;

    // Button B state
    uint8_t mBtnBHold = 0;

    TaskHandle_t mTaskHandle = nullptr;
};

} // namespace Arcana::Io
