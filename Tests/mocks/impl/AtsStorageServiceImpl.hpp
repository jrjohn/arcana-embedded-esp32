#pragma once
// Stub of AtsStorageServiceImpl for unit tests.
//
// Shadows components/AtsStorageService/include/impl/AtsStorageServiceImpl.hpp
// because Tests/mocks is FIRST in the include path.
//
// Provides only the API surface that RegistrationServiceImpl +
// HttpUploadServiceImpl reference. Test injection state lives in
// the Arcana::Storage::test namespace below.

#include "AtsStorageService.hpp"
#include "esp_err.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Arcana::Storage {

class AtsStorageServiceImpl : public AtsStorageService {
public:
    static AtsStorageServiceImpl& getInstance() {
        static AtsStorageServiceImpl s;
        return s;
    }

    esp_err_t init_HAL() override { return ESP_OK; }
    esp_err_t init() override     { return ESP_OK; }
    esp_err_t start() override    { return ESP_OK; }
    void      stop() override     {}

    uint16_t queryByDate(uint32_t, Sensor::SensorData*, uint16_t) override {
        return 0;
    }

    struct PendingFile {
        char name[16];
        uint32_t size;
        uint32_t date;
    };
    static const uint8_t MAX_PENDING = 8;

    uint8_t listPendingUploads(PendingFile* out, uint8_t maxCount) {
        uint8_t n = mPendingCount < maxCount ? mPendingCount : maxCount;
        for (uint8_t i = 0; i < n; i++) {
            snprintf(out[i].name, sizeof(out[i].name), "pending%u.ats", i);
            out[i].size = 1024;
            out[i].date = 20260101 + i;
        }
        return n;
    }
    bool isDateUploaded(uint32_t)  { return false; }
    void markUploaded(uint32_t)    {}

    bool isReady() const { return mDbReady; }

    void pauseRecording()  { mUploadPause = true; }
    void resumeRecording() { mUploadPause = false; }

    bool loadTzConfig(int16_t&, uint8_t&) { return false; }
    bool saveTzConfig(int16_t, uint8_t)   { return false; }

    bool loadCredentials(uint8_t* outBuf, uint16_t bufSize, uint16_t& outLen) {
        if (!mLoadOk) return false;
        uint16_t n = mLoadLen < bufSize ? mLoadLen : bufSize;
        memcpy(outBuf, mLoadData, n);
        outLen = n;
        return true;
    }
    bool saveCredentials(const uint8_t* data, uint16_t len) {
        if (!mSaveOk) return false;
        uint16_t n = len < sizeof(mLastSaveData) ? len : sizeof(mLastSaveData);
        memcpy(mLastSaveData, data, n);
        mLastSaveLen = n;
        return true;
    }

    // ── Test injection helpers ──────────────────────────────────────────────
    void test_setReady(bool ready)     { mDbReady = ready; }
    void test_setLoadOk(bool ok)       { mLoadOk = ok; }
    void test_setSaveOk(bool ok)       { mSaveOk = ok; }
    void test_setPendingCount(uint8_t n) { mPendingCount = n; }
    void test_setLoadData(const uint8_t* data, uint16_t len) {
        uint16_t n = len < sizeof(mLoadData) ? len : sizeof(mLoadData);
        memcpy(mLoadData, data, n);
        mLoadLen = n;
    }
    void test_reset() {
        mDbReady = false;
        mLoadOk = false;
        mSaveOk = false;
        mPendingCount = 0;
        memset(mLoadData, 0, sizeof(mLoadData));
        mLoadLen = 0;
        memset(mLastSaveData, 0, sizeof(mLastSaveData));
        mLastSaveLen = 0;
        mUploadPause = false;
    }
    uint16_t test_lastSaveLen() const { return mLastSaveLen; }
    const uint8_t* test_lastSaveData() const { return mLastSaveData; }

private:
    AtsStorageServiceImpl() = default;

    bool mDbReady = false;
    bool mLoadOk = false;
    bool mSaveOk = false;
    bool mUploadPause = false;
    uint8_t mPendingCount = 0;
    uint8_t mLoadData[256] = {0};
    uint16_t mLoadLen = 0;
    uint8_t mLastSaveData[256] = {0};
    uint16_t mLastSaveLen = 0;
};

} // namespace Arcana::Storage
