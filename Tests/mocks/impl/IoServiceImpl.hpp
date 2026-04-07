#pragma once
// Stub of IoServiceImpl for unit tests.

#include "IoService.hpp"
#include "esp_err.h"

namespace Arcana::Io {

class IoServiceImpl : public IoService {
public:
    static IoServiceImpl& getInstance() {
        static IoServiceImpl s;
        return s;
    }

    esp_err_t init_HAL() override { return ESP_OK; }
    esp_err_t init() override     { return ESP_OK; }
    esp_err_t start() override    { return ESP_OK; }

    bool isUploadRequested() const override { return mUploadRequested; }
    void clearUploadRequest() override      { mUploadRequested = false; }
    bool isCancelRequested() const override { return mCancelRequested; }
    void clearCancelRequest() override      { mCancelRequested = false; }
    bool isFormatRequested() const override { return mFormatRequested; }
    void clearFormatRequest() override      { mFormatRequested = false; }
    void armCancel() override               { mCancelArmed = true; }
    void disarmCancel() override            { mCancelArmed = false; mCancelRequested = false; }

    // Test injection
    void test_setCancelRequested(bool b) { mCancelRequested = b; }
    void test_reset() {
        mUploadRequested = false;
        mCancelRequested = false;
        mFormatRequested = false;
        mCancelArmed = false;
    }

private:
    IoServiceImpl() = default;

    bool mUploadRequested = false;
    bool mCancelRequested = false;
    bool mFormatRequested = false;
    bool mCancelArmed = false;
};

} // namespace Arcana::Io
