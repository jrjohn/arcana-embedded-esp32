#pragma once

#include "AtsStorageService.hpp"
#include "ats/ArcanaTsDb.hpp"
#include "FatFsFilePort.hpp"   // raw FatFs (64-bit f_lseek) — replaces stdio VfsFilePort's 2GB cap
#include "FreeRtosMutex.hpp"
#include "Esp32AesCtrCipher.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

namespace Arcana::Storage {

/**
 * ArcanaTS-based storage service for ESP32.
 *
 * - Single .ats file per day on SD card (FAT32, SPI mode)
 * - Multi-channel: ADS1298 8ch ECG + error log
 * - Block I/O: 4KB blocks, ~140 records/block (ADS1298 = 28 bytes/rec, tagged multi-channel)
 * - AES-256-CTR encryption (Esp32AesCtrCipher), CRC-32 integrity
 * - Daily midnight rotation
 * - Permanent device.ats for lifecycle/config/credentials
 */
class AtsStorageServiceImpl : public AtsStorageService {
public:
    static AtsStorageService& getInstance();

    esp_err_t init_HAL() override;
    esp_err_t init() override;
    esp_err_t start() override;
    void stop() override;

    uint16_t queryByDate(uint32_t dateYYYYMMDD,
                         Sensor::SensorData* out,
                         uint16_t maxCount) override;

    /// Upload support
    struct PendingFile {
        char name[16];      ///< "YYYYMMDD.ats"
        uint32_t size;
        uint32_t date;      ///< YYYYMMDD
    };
    static const uint8_t MAX_PENDING = 8;

    uint8_t listPendingUploads(PendingFile* out, uint8_t maxCount);
    bool isDateUploaded(uint32_t dateYYYYMMDD);
    void markUploaded(uint32_t dateYYYYMMDD);

    bool isReady() const { return mDbReady; }

    /// Cooperative pause/resume for upload
    void pauseRecording()  { mUploadPause = true; }
    void resumeRecording() { mUploadPause = false; }

    /// Timezone config (device.ats CONFIG channel)
    bool loadTzConfig(int16_t& offsetMin, uint8_t& autoCheck);
    bool saveTzConfig(int16_t offsetMin, uint8_t autoCheck);

    /// Credentials (device.ats CREDS channel, encrypted)
    bool loadCredentials(uint8_t* outBuf, uint16_t bufSize, uint16_t& outLen);
    bool saveCredentials(const uint8_t* data, uint16_t len);

private:
    AtsStorageServiceImpl();
    ~AtsStorageServiceImpl();
    AtsStorageServiceImpl(const AtsStorageServiceImpl&) = delete;
    AtsStorageServiceImpl& operator=(const AtsStorageServiceImpl&) = delete;

    // Dedicated task
    static void storageTask(void* param);
    void taskLoop();

    // Daily rotation
    bool openDailyDb();
    void rotateDailyDb(uint32_t lastDay);

    // Device DB
    bool openDeviceDb();
    bool openDeviceDbSafe(const arcana::ats::AtsConfig& cfg);
    bool initDeviceDbChannels();
    void writeLifecycleEvent(uint8_t eventType, uint32_t param);

    // ADS1298 8ch ECG: ts(U32) + 8×I24 = 28 bytes, 145 rec/block
    static const uint16_t RECORD_SIZE = 28;
    void serializeEcgRecord(uint32_t ts, uint16_t ecgPhase, uint8_t* buf);

    // ArcanaTS sensor DB (daily rotation)
    arcana::ats::ArcanaTsDb mDb;
    arcana::ats::FatFsFilePort mFilePort;
    arcana::ats::FreeRtosMutex mMutex;
    arcana::ats::Esp32AesCtrCipher mCipher;

    // ArcanaTS device DB (permanent)
    arcana::ats::ArcanaTsDb mDeviceDb;
    arcana::ats::FatFsFilePort mDeviceFilePort;

    // Buffers
    static uint8_t sSlowBuf[arcana::ats::BLOCK_SIZE];
    static uint8_t sReadCache[arcana::ats::BLOCK_SIZE];
    static uint8_t sDevSlowBuf[arcana::ats::BLOCK_SIZE];
    // Dedicated double-buffer for the primary (ECG) channel: one fills while the
    // other is flushed to SD, so write stalls are absorbed instead of dropping.
    static uint8_t sPrimaryBufA[arcana::ats::BLOCK_SIZE];
    static uint8_t sPrimaryBufB[arcana::ats::BLOCK_SIZE];

    // Per-device encryption key
    static uint8_t sKey[32];  ///< 256-bit AES key

    // Task
    TaskHandle_t mTaskHandle = nullptr;
    bool mRunning = false;
    bool mDbReady = false;
    bool mDeviceDbReady = false;
    volatile bool mUploadPause = false;
    volatile bool mFormatRequested = false;

    // Pending write data
    Sensor::SensorData mPendingData;
    SemaphoreHandle_t mWriteSem = nullptr;

    // Stats
    Observable<StorageStats> mStatsObs;
    StorageStats mStatsModel;
    uint32_t mTotalRecords = 0;
};

} // namespace Arcana::Storage
