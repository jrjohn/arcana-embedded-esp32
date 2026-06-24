#pragma once
// Stub of AtsStorageServiceImpl for unit tests.
//
// Shadows main/service/impl/AtsStorageServiceImpl.hpp
// because Tests/mocks is FIRST in the include path.
//
// Provides only the API surface that RegistrationServiceImpl +
// HttpUploadServiceImpl reference. Test injection state lives in
// the Arcana::Storage::test namespace below.

#include "AtsStorageService.hpp"
#include "ats/IFilePort.hpp"
#include "ats/IMutex.hpp"
#include "esp_err.h"
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Arcana::Storage {
// Fully-inline host read port (reads /sdcard fixtures) standing in for the
// production ExFatFilePort. Inline so it adds no link dependency to the many
// test targets that include this mock (a concrete .cpp-backed port would need
// its vtable linked everywhere).
class HostReadPort : public arcana::ats::IFilePort {
public:
    bool open(const char* path, uint8_t) override {
        char p[80]; snprintf(p, sizeof(p), "/sdcard/%s", path);
        mFp = fopen(p, "rb"); return mFp != nullptr;
    }
    bool close() override { if (mFp) { fclose(mFp); mFp = nullptr; } mRead = 0; return true; }
    // sFailReadAt: once cumulative bytes read reach it, return -1 (simulate a mid-
    // stream SD read error) so the uploader's read-error path is testable.
    static inline int32_t sFailReadAt = -1;
    int32_t read(uint8_t* b, uint32_t n) override {
        if (!mFp) return -1;
        if (sFailReadAt >= 0 && mRead >= sFailReadAt) return -1;
        int32_t r = (int32_t)fread(b, 1, n, mFp);
        if (r > 0) mRead += r;
        return r;
    }
    int32_t write(const uint8_t*, uint32_t) override { return -1; }
    bool seek(uint64_t o) override { return mFp && fseek(mFp, (long)o, SEEK_SET) == 0; }
    bool sync() override { return true; }
    uint64_t tell() override { return mFp ? (uint64_t)ftell(mFp) : 0; }
    uint64_t size() override {
        if (!mFp) return 0;
        long cur = ftell(mFp); fseek(mFp, 0, SEEK_END);
        long s = ftell(mFp); fseek(mFp, cur, SEEK_SET); return (uint64_t)s;
    }
    bool truncate() override { return true; }
    bool isOpen() const override { return mFp != nullptr; }
private:
    FILE* mFp = nullptr;
    int32_t mRead = 0;
};
} // namespace Arcana::Storage

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

    // Upload read path: production returns an ExFatFilePort on the exFAT volume +
    // the SD mutex. The stub returns a host VfsFilePort (reads /sdcard fixtures)
    // and a no-op mutex, so HttpUploadServiceImpl's read loop is testable on the
    // host without SdFat.
    arcana::ats::IFilePort* uploadReadPort() { return &mUploadPort; }
    arcana::ats::IMutex*    sdMutex()        { return &mSdMutex; }

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

    // No-op mutex for the host upload test (no real concurrency in-process).
    struct NopMutex : public arcana::ats::IMutex {
        bool lock(uint32_t = 0xFFFFFFFF) override { return true; }
        void unlock() override {}
    };
    NopMutex mSdMutex;
    HostReadPort mUploadPort;   // reads /sdcard/<name> fixtures

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
