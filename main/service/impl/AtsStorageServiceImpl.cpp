#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "ats/ArcanaTsSchema.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <unistd.h>

static const char* TAG = "AtsStorage";

// SD card SPI pin configuration
#ifndef CONFIG_ATS_SD_CLK_GPIO
#define CONFIG_ATS_SD_CLK_GPIO   4
#endif
#ifndef CONFIG_ATS_SD_MOSI_GPIO
#define CONFIG_ATS_SD_MOSI_GPIO  32
#endif
#ifndef CONFIG_ATS_SD_MISO_GPIO
#define CONFIG_ATS_SD_MISO_GPIO  17
#endif
#ifndef CONFIG_ATS_SD_CS_GPIO
#define CONFIG_ATS_SD_CS_GPIO    27
#endif

static const char* MOUNT_POINT = "/sdcard";

namespace Arcana::Storage {

// Static storage
uint8_t AtsStorageServiceImpl::sKey[32] = {};
uint8_t AtsStorageServiceImpl::sSlowBuf[arcana::ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sReadCache[arcana::ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sDevSlowBuf[arcana::ats::BLOCK_SIZE] = {};

// Time source for ArcanaTS
static uint32_t atsGetTime() {
    time_t now;
    time(&now);
    return (now > 1577836800) ? (uint32_t)now   // valid epoch (> 2020-01-01)
                              : (uint32_t)(xTaskGetTickCount() / configTICK_RATE_HZ);
}

// Derive per-device encryption key from MAC address
static void deriveKey(uint8_t key[32]) {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    // Simple key derivation: repeat MAC to fill 32 bytes + XOR with constant
    for (int i = 0; i < 32; i++) {
        key[i] = mac[i % 6] ^ (uint8_t)(0xA5 + i);
    }
}

// ---------------------------------------------------------------------------
// Singleton + lifecycle
// ---------------------------------------------------------------------------

AtsStorageServiceImpl::AtsStorageServiceImpl()
    : mFilePort(MOUNT_POINT)
    , mDeviceFilePort(MOUNT_POINT)
    , mStatsObs()
{
    output.StatsEvents = &mStatsObs;
}

AtsStorageServiceImpl::~AtsStorageServiceImpl() {
    stop();
}

AtsStorageService& AtsStorageServiceImpl::getInstance() {
    static AtsStorageServiceImpl sInstance;
    return sInstance;
}

esp_err_t AtsStorageServiceImpl::init_HAL() {
    // Derive per-device encryption key
    deriveKey(sKey);

    // Initialize SPI bus
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = CONFIG_ATS_SD_MOSI_GPIO;
    bus_cfg.miso_io_num = CONFIG_ATS_SD_MISO_GPIO;
    bus_cfg.sclk_io_num = CONFIG_ATS_SD_CLK_GPIO;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4096;

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Mount SD card via FatFS
    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = true;
    mount_cfg.max_files = 4;
    mount_cfg.allocation_unit_size = 16 * 1024;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.max_freq_khz = 4000;  // 4 MHz — safe for long wires + level shifter
    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = (gpio_num_t)CONFIG_ATS_SD_CS_GPIO;
    slot_cfg.host_id = SPI2_HOST;

    sdmmc_card_t* card = nullptr;
    ret = esp_vfs_fat_sdspi_mount(MOUNT_POINT, &host, &slot_cfg, &mount_cfg, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SD card mounted at %s (%s, %lluMB)",
             MOUNT_POINT, card->cid.name,
             (unsigned long long)(((uint64_t)card->csd.capacity)
                                  * card->csd.sector_size / (1024 * 1024)));

    return ESP_OK;
}

esp_err_t AtsStorageServiceImpl::init() {
    mWriteSem = xSemaphoreCreateBinary();
    if (!mWriteSem) return ESP_ERR_NO_MEM;

    mMutex.init();
    return ESP_OK;
}

esp_err_t AtsStorageServiceImpl::start() {
    mRunning = true;

    BaseType_t ret = xTaskCreate(
        storageTask, "AtsStore", 4096,
        this, tskIDLE_PRIORITY + 1, &mTaskHandle);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;

    // Sensor subscription not needed for ECG benchmark mode —
    // taskLoop generates synthetic 1KHz ECG data internally.

    return ESP_OK;
}

void AtsStorageServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        xSemaphoreGive(mWriteSem);
        vTaskDelay(pdMS_TO_TICKS(200));
        mTaskHandle = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Storage task
// ---------------------------------------------------------------------------

void AtsStorageServiceImpl::storageTask(void* param) {
    auto* self = static_cast<AtsStorageServiceImpl*>(param);
    bool formatRequested = false;

    for (;;) {
        // Open device.ats (permanent lifecycle DB)
        if (!self->mDeviceDbReady && self->openDeviceDb()) {
            self->writeLifecycleEvent(
                static_cast<uint8_t>(arcana::ats::LifecycleEventType::PowerOn), 0);

            int16_t tzOff = 0;
            uint8_t tzAuto = 1;
            if (self->loadTzConfig(tzOff, tzAuto)) {
                ESP_LOGI(TAG, "Timezone offset: %d min", tzOff);
            }
        }

        // Open daily sensor DB
        if (!self->mDbReady) {
            ESP_LOGI(TAG, "Opening daily sensor DB...");
            if (!self->openDailyDb()) {
                ESP_LOGE(TAG, "Failed to open daily sensor DB");
                self->mStatsModel = {};
                self->mStatsObs.Notify(self->mStatsModel);
                vTaskDelete(nullptr);
                return;
            }
        }

        self->mTotalRecords = self->mDb.getStats().totalRecords;
        ESP_LOGI(TAG, "ATS ready, %lu records recovered",
                 (unsigned long)self->mTotalRecords);

        self->mStatsModel.recordCount = self->mTotalRecords;
        self->mStatsModel.totalKB = (self->mDb.getStats().blocksWritten + 1) * 4;
        self->mStatsObs.Notify(self->mStatsModel);

        self->mRunning = true;
        self->taskLoop();
        // taskLoop exited — either stop() called or format requested

        // Close DBs
        ESP_LOGI(TAG, "Closing databases...");
        if (self->mDbReady) {
            self->mDb.close();
            self->mDbReady = false;
        }
        if (self->mDeviceDbReady) {
            self->writeLifecycleEvent(
                static_cast<uint8_t>(arcana::ats::LifecycleEventType::PowerOff), 0);
            self->mDeviceDb.close();
            self->mDeviceDbReady = false;
        }

        // Check if format was requested (set by taskLoop)
        formatRequested = self->mFormatRequested;
        self->mFormatRequested = false;

        if (!formatRequested) {
            // --- Safe eject: unmount, wait for Button A to resume ---
            ESP_LOGI(TAG, "Safe to remove SD card. Press Button A to resume.");

            // Unmount FatFS VFS
            esp_vfs_fat_sdcard_unmount(MOUNT_POINT, nullptr);

            // Wait for Button A press to remount
            auto& ioSvc = Io::IoServiceImpl::getInstance();
            ioSvc.clearUploadRequest();
            bool btnSeen = false;
            for (;;) {
                vTaskDelay(pdMS_TO_TICKS(100));
                if (ioSvc.isUploadRequested()) {
                    ioSvc.clearUploadRequest();
                    if (btnSeen) break;  // need release-first
                }
                if (!ioSvc.isUploadRequested()) btnSeen = true;
            }

            ESP_LOGI(TAG, "Remounting SD card...");
            vTaskDelay(pdMS_TO_TICKS(1000));  // debounce + card settle

            // Remount SD card (re-init SPI + FatFS)
            if (self->init_HAL() != ESP_OK) {
                ESP_LOGE(TAG, "SD remount failed — waiting for reset");
                vTaskDelete(nullptr);
                return;
            }

            ESP_LOGI(TAG, "SD card remounted — reopening databases");
            // Loop back → reopen DBs
            continue;
        }

        // --- Format: delete all .ats files and recreate ---
        ESP_LOGW(TAG, "Formatting SD card — deleting all .ats files...");
        remove("/sdcard/sensor.ats");
        remove("/sdcard/device.ats");

        // Also delete any daily rotation files (YYYYMMDD.ats)
        DIR* dir = opendir(MOUNT_POINT);
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != nullptr) {
                const char* name = entry->d_name;
                size_t len = strlen(name);
                if (len >= 4 && strcmp(name + len - 4, ".ats") == 0) {
                    char path[300];
                    snprintf(path, sizeof(path), "%s/%s", MOUNT_POINT, name);
                    remove(path);
                    ESP_LOGI(TAG, "Deleted: %s", name);
                }
            }
            closedir(dir);
        }

        ESP_LOGI(TAG, "Format complete — reopening databases");
        self->mTotalRecords = 0;

        // Loop back → reopen fresh DBs
    }

    vTaskDelete(nullptr);
}

// Synthetic ECG waveform LUT (one heartbeat, 250 samples @ 250Hz = 1 sec = 60 BPM)
static const uint8_t ECG_LUT[] = {
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,69,68,66,64,63,63,64,66, 68,69,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,72,75, 70,50,25, 5, 0, 5,25,50,80,90,
    85,78,72,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,69,67,64,61,58,56,55,55,56, 58,61,64,67,69,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70, 70,70,70,70,70,70,70,70,70,70,
    70,70,70,70,70,70,70,70,70,70,
};
static const uint16_t ECG_LUT_LEN = sizeof(ECG_LUT);

void AtsStorageServiceImpl::taskLoop() {
    uint32_t lastDay = 0;
    uint32_t lastReportTick = xTaskGetTickCount();
    uint32_t windowOk = 0;
    uint32_t windowFail = 0;
    uint16_t ecgPhase = 0;
    TickType_t nextWake = xTaskGetTickCount();

    while (mRunning) {
        // Cooperative pause for upload
        while (mUploadPause && mRunning) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // 1KHz pacing — 1 record per ms
        vTaskDelayUntil(&nextWake, 1);

        if (!mDbReady) continue;

        // Build synthetic ECG record (28 bytes: ts + 8×I24)
        uint8_t rec[RECORD_SIZE];
        serializeEcgRecord(atsGetTime(), ecgPhase, rec);
        ecgPhase++;
        if (ecgPhase >= ECG_LUT_LEN) ecgPhase = 0;

        if (mDb.append(0, rec)) {
            mTotalRecords++;
            windowOk++;
        } else {
            windowFail++;
        }

        // Report + flush every 1 second
        uint32_t now = xTaskGetTickCount();
        if ((now - lastReportTick) >= pdMS_TO_TICKS(1000)) {
            mDb.flush();

            mStatsModel.recordCount = mTotalRecords;
            mStatsModel.writesPerSec = (uint16_t)windowOk;
            mStatsModel.totalKB = (mDb.getStats().blocksWritten + 1) * 4;
            mStatsModel.kbPerSec = (uint16_t)(windowOk * RECORD_SIZE / 1024);
            mStatsObs.Notify(mStatsModel);

            ESP_LOGI(TAG, "ECG 8ch: %u rec/s, %u KB/s, total=%lu, fail=%lu",
                     (unsigned)windowOk,
                     (unsigned)(windowOk * RECORD_SIZE / 1024),
                     (unsigned long)mTotalRecords, (unsigned long)windowFail);

            windowOk = 0;
            windowFail = 0;
            lastReportTick = now;

            // Midnight rotation
            time_t t;
            time(&t);
            if (t > 1577836800) {
                struct tm tm;
                gmtime_r(&t, &tm);
                uint32_t today = (uint32_t)((tm.tm_year + 1900) * 10000
                                            + (tm.tm_mon + 1) * 100
                                            + tm.tm_mday);
                if (lastDay != 0 && today != lastDay) {
                    rotateDailyDb(lastDay);
                }
                lastDay = today;
            }

            // Button B long press → format SD
            // (Button A upload is handled by AppContainer upload_mon task)
            {
                auto& ioSvc = Io::IoServiceImpl::getInstance();
                if (ioSvc.isFormatRequested()) {
                    ioSvc.clearFormatRequest();
                    ESP_LOGW(TAG, "Format requested — stopping recording");
                    mFormatRequested = true;
                    mRunning = false;
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Daily DB
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::openDailyDb() {
    arcana::ats::AtsConfig cfg = {};

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    cfg.file = &mFilePort;
    cfg.cipher = &mCipher;
    cfg.mutex = &mMutex;
    cfg.getTime = atsGetTime;
    cfg.key = sKey;
    cfg.headerKey = nullptr;  // no header encryption for now
    cfg.deviceUid = mac;
    cfg.deviceUidSize = 6;
    cfg.overflow = arcana::ats::OverflowPolicy::Drop;
    cfg.primaryChannel = 0xFF;
    cfg.primaryBufA = nullptr;
    cfg.primaryBufB = nullptr;
    cfg.slowBuf = sSlowBuf;
    cfg.readCache = sReadCache;

    if (!mDb.open("sensor.ats", cfg)) {
        ESP_LOGW(TAG, "sensor.ats open failed, recreating");
        remove("/sdcard/sensor.ats");
        if (!mDb.open("sensor.ats", cfg)) {
            ESP_LOGE(TAG, "sensor.ats recreate failed");
            return false;
        }
    }

    // Add channels if new file
    if (!mDb.isReadOnly() && mDb.getChannelCount() == 0) {
        arcana::ats::ArcanaTsSchema sensor = arcana::ats::ArcanaTsSchema::ads1298_8ch();
        if (!mDb.addChannel(0, sensor)) {
            ESP_LOGE(TAG, "addChannel(0, dht11) failed");
            mDb.close();
            return false;
        }

        arcana::ats::ArcanaTsSchema errLog = arcana::ats::ArcanaTsSchema::errorLog();
        if (!mDb.addChannel(1, errLog)) {
            ESP_LOGE(TAG, "addChannel(1, errorLog) failed");
            mDb.close();
            return false;
        }

        if (!mDb.start()) {
            ESP_LOGE(TAG, "sensor DB start() failed");
            mDb.close();
            return false;
        }
    }

    mDbReady = true;
    ESP_LOGI(TAG, "sensor.ats opened, %lu records",
             (unsigned long)mDb.getStats().totalRecords);
    return true;
}

void AtsStorageServiceImpl::rotateDailyDb(uint32_t lastDay) {
    ESP_LOGI(TAG, "Rotating daily DB, day=%lu", (unsigned long)lastDay);

    mDb.close();
    mDbReady = false;

    char oldPath[64], newPath[64];
    snprintf(oldPath, sizeof(oldPath), "%s/sensor.ats", MOUNT_POINT);
    snprintf(newPath, sizeof(newPath), "%s/%08lu.ats", MOUNT_POINT, (unsigned long)lastDay);
    rename(oldPath, newPath);

    if (!openDailyDb()) {
        ESP_LOGE(TAG, "Failed to open new daily DB after rotation");
    }
}

// ---------------------------------------------------------------------------
// Device DB
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::initDeviceDbChannels() {
    arcana::ats::ArcanaTsSchema lc = arcana::ats::ArcanaTsSchema::lifecycleEvent();
    if (!mDeviceDb.addChannel(0, lc)) return false;
    arcana::ats::ArcanaTsSchema cfgSchema = arcana::ats::ArcanaTsSchema::config();
    mDeviceDb.addChannel(1, cfgSchema);
    arcana::ats::ArcanaTsSchema creds = arcana::ats::ArcanaTsSchema::credentials();
    mDeviceDb.addChannel(2, creds);
    return mDeviceDb.start();
}

bool AtsStorageServiceImpl::openDeviceDbSafe(const arcana::ats::AtsConfig& cfg) {
    if (mDeviceDb.open("device.ats", cfg)) {
        // Live upgrade: add missing channels
        if (mDeviceDb.getChannelCount() > 0 && mDeviceDb.getChannelCount() < 3) {
            arcana::ats::ArcanaTsSchema creds = arcana::ats::ArcanaTsSchema::credentials();
            if (mDeviceDb.addChannelLive(2, creds)) {
                ESP_LOGI(TAG, "device.ats upgraded: added CREDS channel");
            }
        }
        return true;
    }

    // Corrupt → recreate
    ESP_LOGW(TAG, "device.ats corrupt, recreating");
    remove("/sdcard/device_old.ats");
    rename("/sdcard/device.ats", "/sdcard/device_old.ats");
    if (mDeviceDb.open("device.ats", cfg)) {
        if (initDeviceDbChannels()) return true;
        mDeviceDb.close();
    }
    return false;
}

bool AtsStorageServiceImpl::openDeviceDb() {
    arcana::ats::AtsConfig cfg = {};

    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);

    cfg.file = &mDeviceFilePort;
    cfg.cipher = &mCipher;
    cfg.mutex = &mMutex;
    cfg.getTime = atsGetTime;
    cfg.key = sKey;
    cfg.headerKey = nullptr;
    cfg.deviceUid = mac;
    cfg.deviceUidSize = 6;
    cfg.overflow = arcana::ats::OverflowPolicy::Drop;
    cfg.primaryChannel = 0xFF;
    cfg.slowBuf = sDevSlowBuf;
    cfg.readCache = sReadCache;

    if (!openDeviceDbSafe(cfg)) {
        ESP_LOGE(TAG, "device.ats unavailable");
        return true;  // never block boot
    }

    if (mDeviceDb.getChannelCount() == 0) {
        if (!initDeviceDbChannels()) {
            ESP_LOGE(TAG, "device.ats channel init failed");
            return true;  // degraded
        }
    }

    mDeviceDbReady = true;
    ESP_LOGI(TAG, "device.ats opened, %lu records, %u channels",
             (unsigned long)mDeviceDb.getStats().totalRecords,
             mDeviceDb.getChannelCount());
    return true;
}

void AtsStorageServiceImpl::writeLifecycleEvent(uint8_t eventType, uint32_t param) {
    if (!mDeviceDbReady) return;

    // LIFECYCLE schema: ts(U32), evtTyp(U8), evtCod(U16), rsv(U8), param(U32) = 12 bytes
    uint8_t rec[12];
    uint32_t ts = atsGetTime();
    memcpy(rec, &ts, 4);
    rec[4] = eventType;
    rec[5] = 0; rec[6] = 0;
    rec[7] = 0;
    memcpy(rec + 8, &param, 4);

    mDeviceDb.append(0, rec);
    mDeviceDb.flush();
}

// ---------------------------------------------------------------------------
// Record serialization (DHT11: 8 bytes)
// ---------------------------------------------------------------------------

void AtsStorageServiceImpl::serializeEcgRecord(uint32_t ts, uint16_t ecgPhase,
                                                uint8_t* buf) {
    // ADS1298 schema: ts(U32) + 8×ch(I24) = 4 + 24 = 28 bytes
    memcpy(buf, &ts, 4);

    // Generate 8 channels from ECG LUT with phase offset per channel
    for (int ch = 0; ch < 8; ch++) {
        uint16_t idx = (ecgPhase + ch * 31) % ECG_LUT_LEN;  // phase-shifted
        int32_t val = ((int32_t)ECG_LUT[idx] - 70) * 8000;  // scale to ±560000 (~20-bit range)

        // I24 little-endian (3 bytes)
        buf[4 + ch * 3 + 0] = (uint8_t)(val >>  0);
        buf[4 + ch * 3 + 1] = (uint8_t)(val >>  8);
        buf[4 + ch * 3 + 2] = (uint8_t)(val >> 16);
    }
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

uint16_t AtsStorageServiceImpl::queryByDate(uint32_t dateYYYYMMDD,
                                             Sensor::SensorData* out,
                                             uint16_t maxCount) {
    if (!mDbReady) return 0;

    // Howard Hinnant algorithm: YYYYMMDD → epoch range
    uint32_t y = dateYYYYMMDD / 10000;
    uint32_t m = (dateYYYYMMDD / 100) % 100;
    uint32_t d = dateYYYYMMDD % 100;
    if (m <= 2) { y--; m += 9; } else { m -= 3; }
    uint32_t era = y / 400;
    uint32_t yoe = y - era * 400;
    uint32_t doy = (153 * m + 2) / 5 + d - 1;
    uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int32_t days = (int32_t)(era * 146097 + doe) - 719468;
    uint32_t dayStart = (uint32_t)days * 86400;
    uint32_t dayEnd = dayStart + 86400 - 1;

    struct QueryCtx {
        Sensor::SensorData* out;
        uint16_t maxCount;
        uint16_t count;
    };

    QueryCtx ctx = { out, maxCount, 0 };

    mDb.queryByTime(0, dayStart, dayEnd,
        [](uint8_t, const uint8_t* rec, uint32_t, void* arg) -> bool {
            auto* c = static_cast<QueryCtx*>(arg);
            if (c->count >= c->maxCount) return true;

            auto& s = c->out[c->count];
            memcpy(&s.TimestampMs, rec, 4);  // reuse as epoch
            int16_t tempCenti, humiCenti;
            memcpy(&tempCenti, rec + 4, 2);
            memcpy(&humiCenti, rec + 6, 2);
            s.Temperature = (float)tempCenti / 100.0f;
            s.Humidity = (float)humiCenti / 100.0f;
            c->count++;
            return false;
        }, &ctx);

    return ctx.count;
}

// ---------------------------------------------------------------------------
// Upload support
// ---------------------------------------------------------------------------

uint8_t AtsStorageServiceImpl::listPendingUploads(PendingFile* out, uint8_t maxCount) {
    uint8_t count = 0;
    DIR* dir = opendir(MOUNT_POINT);
    if (!dir) return 0;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr && count < maxCount) {
        const char* name = entry->d_name;
        size_t len = strlen(name);
        if (len != 12) continue;
        if (name[8] != '.' || name[9] != 'a' || name[10] != 't' || name[11] != 's') continue;
        if (strcmp(name, "sensor.ats") == 0 || strcmp(name, "device.ats") == 0) continue;

        uint32_t date = 0;
        bool valid = true;
        for (int i = 0; i < 8; i++) {
            if (name[i] < '0' || name[i] > '9') { valid = false; break; }
            date = date * 10 + (name[i] - '0');
        }
        if (!valid || date < 20200101) continue;
        if (isDateUploaded(date)) continue;

        strncpy(out[count].name, name, 15);
        out[count].name[15] = '\0';
        out[count].size = 0;  // size not available from dirent
        out[count].date = date;
        count++;
    }
    closedir(dir);
    return count;
}

bool AtsStorageServiceImpl::isDateUploaded(uint32_t dateYYYYMMDD) {
    if (!mDeviceDbReady) return false;

    static const uint16_t REC_SIZE = 12;
    static const uint16_t SCAN_COUNT = arcana::ats::BLOCK_SIZE / REC_SIZE;
    uint8_t* buf = sReadCache;

    uint16_t n = mDeviceDb.queryLatest(0, buf, SCAN_COUNT);
    for (uint16_t i = 0; i < n; i++) {
        uint8_t* rec = buf + i * REC_SIZE;
        uint8_t evtType = rec[4];
        uint32_t param;
        memcpy(&param, rec + 8, 4);

        if (evtType == static_cast<uint8_t>(arcana::ats::LifecycleEventType::UploadDone) &&
            param == dateYYYYMMDD) {
            return true;
        }
    }
    return false;
}

void AtsStorageServiceImpl::markUploaded(uint32_t dateYYYYMMDD) {
    writeLifecycleEvent(
        static_cast<uint8_t>(arcana::ats::LifecycleEventType::UploadDone),
        dateYYYYMMDD);
}

// ---------------------------------------------------------------------------
// Timezone config
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::loadTzConfig(int16_t& offsetMin, uint8_t& autoCheck) {
    if (mDeviceDbReady && mDeviceDb.getChannelCount() > 1) {
        uint8_t buf[17];
        uint16_t n = mDeviceDb.queryLatest(1, buf, 1);
        if (n > 0) {
            memcpy(&offsetMin, buf + 14, 2);
            autoCheck = buf[16];
            return true;
        }
    }
    return false;
}

bool AtsStorageServiceImpl::saveTzConfig(int16_t offsetMin, uint8_t autoCheck) {
    if (mDeviceDbReady && mDeviceDb.getChannelCount() > 1) {
        uint8_t rec[17] = {};
        uint32_t ts = atsGetTime();
        memcpy(rec, &ts, 4);
        memcpy(rec + 14, &offsetMin, 2);
        rec[16] = autoCheck;
        mDeviceDb.append(1, rec);
        mDeviceDb.flush();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Credentials
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::loadCredentials(uint8_t* outBuf, uint16_t bufSize,
                                             uint16_t& outLen) {
    outLen = 0;
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() < 3) return false;

    uint8_t rec[236];
    uint16_t n = mDeviceDb.queryLatest(2, rec, 1);
    if (n == 0) return false;

    uint16_t dataLen = 232;
    if (dataLen > bufSize) dataLen = bufSize;
    memcpy(outBuf, rec + 4, dataLen);
    outLen = dataLen;
    return true;
}

bool AtsStorageServiceImpl::saveCredentials(const uint8_t* data, uint16_t len) {
    ESP_LOGI(TAG, "saveCredentials: ready=%d channels=%u len=%u",
             mDeviceDbReady, mDeviceDb.getChannelCount(), len);
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() < 3) return false;
    if (len > 232) return false;  // max data portion of credentials record

    uint8_t rec[236] = {};
    uint32_t ts = atsGetTime();
    memcpy(rec, &ts, 4);
    memcpy(rec + 4, data, len);

    mDeviceDb.append(2, rec);
    mDeviceDb.flush();
    return true;
}

} // namespace Arcana::Storage
