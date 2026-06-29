#include "impl/AtsStorageServiceImpl.hpp"
#include "impl/IoServiceImpl.hpp"
#include "TaskPriorities.hpp"
#include "ats/ArcanaTsSchema.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/idf_additions.h"   // xQueueCreateWithCaps / vQueueDeleteWithCaps
#include "sdmmc_cmd.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include <cstring>
#include <cstdio>
#include <ctime>

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
#ifndef CONFIG_ATS_SD_MAX_FREQ_KHZ
#define CONFIG_ATS_SD_MAX_FREQ_KHZ 4000
#endif

// SD card + sdspi device handles (singleton service → file-scope is fine).
// Shared by init_HAL (mount) and storageTask (safe-eject teardown/remount).
static sdmmc_card_t      sCard;
static sdspi_dev_handle_t sDevh = 0;

namespace Arcana::Storage {

// Static storage
uint8_t AtsStorageServiceImpl::sKey[32] = {};
uint8_t AtsStorageServiceImpl::sSlowBuf[arcana::ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sReadCache[arcana::ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sPrimaryBufA[arcana::ats::BLOCK_SIZE] = {};
uint8_t AtsStorageServiceImpl::sPrimaryBufB[arcana::ats::BLOCK_SIZE] = {};
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
    // exFAT ports open files on the shared mExFatVol (mounted in init_HAL). The
    // volume object exists now (declared before the ports); begin() runs later.
    : mFilePort(&mExFatVol)
    , mDeviceFilePort(&mExFatVol)
    , mUploadFilePort(&mExFatVol)
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
    if (ret == ESP_ERR_INVALID_STATE) {
        // Bus already initialized — the ST7789 SPI LCD shares SPI2 and its
        // init runs first (LcdService precedes storage in initHAL).
        ESP_LOGI(TAG, "SPI bus already initialized (shared with LCD)");
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // SD card init via ESP-IDF sdspi — NO esp_vfs_fat / FatFs. ESP-IDF's own
    // FatFs exFAT is broken on this board's SPI host; SdFat owns the exFAT
    // filesystem instead (over EspIdfBlockDev). ESP-IDF only inits the card.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    // Per-board ceiling: 4 MHz for long wires + level shifter (ESP32 DevKit),
    // 20 MHz for short direct traces (DNESP32S3 TF slot). See Kconfig.
    host.max_freq_khz = CONFIG_ATS_SD_MAX_FREQ_KHZ;
    // sdmmc bounces non-DMA-capable / unaligned buffers; SdFat's cache is internal
    // RAM but this is belt-and-suspenders for sector I/O alignment.
    host.flags |= SDMMC_HOST_FLAG_ALLOC_ALIGNED_BUF;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = (gpio_num_t)CONFIG_ATS_SD_CS_GPIO;
    slot_cfg.host_id = SPI2_HOST;

    ret = sdspi_host_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "sdspi_host_init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = sdspi_host_init_device(&slot_cfg, &sDevh);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "sdspi_host_init_device failed: %s", esp_err_to_name(ret));
        return ret;
    }
    host.slot = sDevh;

    // Init with retries: a card whose multi-block write was cut off by a reset
    // (the ESP resets, the card keeps power) can stay busy and answer init with
    // garbage (CRC/timeout). NOTE: only a COLD power-cycle reliably clears this;
    // warm DTR/RTS resets can leave the card wedged (send_if_cond 0x108).
    for (int attempt = 1; ; attempt++) {
        ret = sdmmc_card_init(&host, &sCard);
        if (ret == ESP_OK) break;
        if (attempt >= 3) {
            ESP_LOGE(TAG, "SD card init failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ESP_LOGW(TAG, "SD init attempt %d failed (%s) — retrying...",
                 attempt, esp_err_to_name(ret));
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    mBlockDev.setCard(&sCard);

    // Mount the power-fail-safe dual-FAT exFAT via SdFat. Reformat when the card
    // is NOT dual-FAT — i.e. blank/FAT32 (mount fails) OR a legacy single-FAT exFAT
    // (numberOfFats < 2). This migrates the medical logger to the dual-FAT layout so
    // an abrupt power loss can never leave the card unmountable. WARNING: reformat
    // erases existing data (one-time migration; upload/back up the card first).
    bool mounted = mExFatVol.begin(&mBlockDev);
    if (!mounted || mExFatVol.numberOfFats() < 2) {
        ESP_LOGW(TAG, "Formatting card as power-fail-safe dual-FAT exFAT%s",
                 mounted ? " — migrating legacy single-FAT card" : "");
        uint8_t secBuf[512];
        ExFatFormatter formatter;
        if (!formatter.format(&mBlockDev, secBuf, nullptr) || !mExFatVol.begin(&mBlockDev)) {
            ESP_LOGE(TAG, "dual-FAT format/mount failed");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "SD card mounted exFAT (%s, %lluMB, %u FATs)", sCard.cid.name,
             (unsigned long long)(((uint64_t)sCard.csd.capacity)
                                  * sCard.csd.sector_size / (1024 * 1024)),
             mExFatVol.numberOfFats());

    // Heal any "free but referenced" tail a torn dual-FAT commit left behind, but
    // ONLY on the files that can be open-for-write at a power loss: the active
    // sensor + device DBs. Rotated day-files are cleanly closed at rotation, so
    // scanning them is unnecessary (whole-volume reconcile was ~80 s on a card
    // with months of day-files). Must run before any allocation.
    int64_t _trec = esp_timer_get_time();
    mExFatVol.reconcileFile("/sensor.ats");
    mExFatVol.reconcileFile("/device.ats");
    int64_t _trecMs = (esp_timer_get_time() - _trec) / 1000;
    ESP_LOGI(TAG, "dual-FAT reconcile: %lldms (healed %lu cluster(s))",
             (long long)_trecMs, (unsigned long)mExFatVol.healedClusters());

    return ESP_OK;
}

esp_err_t AtsStorageServiceImpl::init() {
    mWriteSem = xSemaphoreCreateBinary();
    if (!mWriteSem) return ESP_ERR_NO_MEM;

    mMutex.init();
    return ESP_OK;
}

// Deep ECG ring depth (records). At 1 kHz this is how long an SD stall can be
// absorbed before the sampler has to back off: S3 ~2 s, classic ESP32 kept lean.
#if CONFIG_IDF_TARGET_ESP32S3
static constexpr UBaseType_t kEcgRingDepth = 2048;   // ~57 KB (28 B/rec), ~2 s @1kHz
#else
static constexpr UBaseType_t kEcgRingDepth = 512;    // ~14 KB, ~0.5 s @1kHz
#endif

esp_err_t AtsStorageServiceImpl::start() {
    mRunning = true;

    // Deep ring between the sampler and the SD writer (Phase 2): a write stall
    // backs up here instead of pausing sampling. ~57 KB at depth 2048 — put it in
    // PSRAM so it doesn't starve the internal heap shared with the WiFi/BT stacks
    // (a plain xQueueCreate of that size aborts with ESP_ERR_NO_MEM after the
    // radios are up). The sampler is a task, not an ISR, so PSRAM storage is fine.
    mEcgQueue = xQueueCreateWithCaps(kEcgRingDepth, RECORD_SIZE,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mEcgQueue) {   // no PSRAM (classic ESP32) — fall back to internal RAM
        mEcgQueue = xQueueCreateWithCaps(kEcgRingDepth, RECORD_SIZE,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    if (!mEcgQueue) return ESP_ERR_NO_MEM;

    // Consumer: drains the ring → mDb.append (medical mode) → SD. Pinned to the
    // APP core so SD write bursts never jitter the WiFi/BT stacks on the PRO core.
    BaseType_t ret = xTaskCreatePinnedToCore(
        storageTask, "AtsStore", 4096,
        this, TaskCfg::kPrioStorage, &mTaskHandle, TaskCfg::kCoreApp);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;

    // Producer: synthetic 1 kHz ECG sampler. Higher priority than the writer so
    // sampling cadence is never delayed by SD drains. (In production this is the
    // ADS1298 DRDY-driven acquisition path.)
    ret = xTaskCreatePinnedToCore(
        sampleTask, "EcgSample", 3072,
        this, TaskCfg::kPrioSampling, &mSampleTaskHandle, TaskCfg::kCoreApp);
    if (ret != pdPASS) return ESP_ERR_NO_MEM;

    return ESP_OK;
}

void AtsStorageServiceImpl::stop() {
    mRunning = false;
    if (mTaskHandle) {
        xSemaphoreGive(mWriteSem);
        vTaskDelay(pdMS_TO_TICKS(200));
        mTaskHandle = nullptr;
    }
    mSampleTaskHandle = nullptr;   // sampleLoop exits on !mRunning
    if (mEcgQueue) {
        vQueueDeleteWithCaps(mEcgQueue);
        mEcgQueue = nullptr;
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

        // Lifetime grand-total counter (device.ats ch3) — load/seed once, then
        // fold forward at each rotation. Runs after both DBs are open.
        if (!self->mLifetimeReady) {
            self->initLifetimeCounter();
        }

        self->mStatsModel.recordCount = self->mTotalRecords;
        self->mStatsModel.lifetimeRecords = self->lifetimeRecordCount();
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

            // Unmount exFAT + tear down the sdspi device so init_HAL can cleanly
            // re-init (supports swapping the card while parked).
            self->mExFatVol.end();
            if (sDevh) { sdspi_host_remove_device(sDevh); sDevh = 0; }
            sdspi_host_deinit();

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
        // Delete every *.ats on the volume root (sensor/device/YYYYMMDD).
        self->fsListAts([](void* ctx, const char* name) {
            auto* s = static_cast<AtsStorageServiceImpl*>(ctx);
            s->fsRemove(name);
            ESP_LOGI(TAG, "Deleted: %s", name);
        }, self);

        ESP_LOGI(TAG, "Format complete — reopening databases");
        self->mTotalRecords = 0;
        // Format wiped every .ats (incl. device.ats) — re-init the lifetime counter
        // on reopen; with no day-files present it correctly restarts from 0.
        self->mLifetimeReady   = false;
        self->mLifetimeRecords = 0;
        self->mLifetimeLastDay = 0;

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

// Consumer task: drain the deep ECG ring → mDb.append (medical mode) → SD.
// The 1 kHz generation lives in sampleLoop() (producer); here we only write, so
// an SD stall backs up in the queue rather than pausing sampling.
void AtsStorageServiceImpl::taskLoop() {
    uint32_t lastDay = 0;
    uint32_t lastReportTick = xTaskGetTickCount();
    uint32_t windowOk = 0;
    uint32_t windowFail = 0;

    while (mRunning) {
        // Cooperative pause for upload
        while (mUploadPause && mRunning) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        if (mDbReady) {
            // Block up to 1 s so the per-second report/flush still runs when the
            // ring is briefly empty.
            uint8_t rec[RECORD_SIZE];
            if (xQueueReceive(mEcgQueue, rec, pdMS_TO_TICKS(1000)) == pdTRUE) {
                if (mDb.append(0, rec)) {
                    mTotalRecords++;
                    windowOk++;
                } else {
                    windowFail++;
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }

        // Report + flush every 1 second
        uint32_t now = xTaskGetTickCount();
        if ((now - lastReportTick) >= pdMS_TO_TICKS(1000)) {
            mDb.flush();

            uint32_t dayRecords = mDb.getStats().totalRecords;  // current sensor.ats (today)
            mStatsModel.recordCount = dayRecords;
            mStatsModel.lifetimeRecords = mLifetimeRecords + dayRecords;  // grand total
            mStatsModel.writesPerSec = (uint16_t)windowOk;
            mStatsModel.totalKB = (mDb.getStats().blocksWritten + 1) * 4;
            mStatsModel.kbPerSec = (uint16_t)(windowOk * RECORD_SIZE / 1024);
            mStatsObs.Notify(mStatsModel);

            ESP_LOGI(TAG, "ECG 8ch: %u rec/s, %u KB/s, day=%lu, lifetime=%llu, fail=%lu, qfull=%lu, qlen=%u",
                     (unsigned)windowOk,
                     (unsigned)(windowOk * RECORD_SIZE / 1024),
                     (unsigned long)dayRecords,
                     (unsigned long long)(mLifetimeRecords + dayRecords),
                     (unsigned long)windowFail,
                     (unsigned long)mEcgQueueDrops,
                     (unsigned)uxQueueMessagesWaiting(mEcgQueue));

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
// Sampler task (producer)
// ---------------------------------------------------------------------------

void AtsStorageServiceImpl::sampleTask(void* param) {
    static_cast<AtsStorageServiceImpl*>(param)->sampleLoop();
    vTaskDelete(nullptr);
}

// Produces synthetic 1 kHz ECG and enqueues into the deep ring. Never touches
// the SD card — so it keeps perfect cadence even while the writer is stalled.
// (In production this is the ADS1298 DRDY-driven acquisition path.)
void AtsStorageServiceImpl::sampleLoop() {
    uint16_t ecgPhase = 0;
    TickType_t nextWake = xTaskGetTickCount();

    while (mRunning) {
        while (mUploadPause && mRunning) {
            vTaskDelay(pdMS_TO_TICKS(10));
            nextWake = xTaskGetTickCount();
        }

        vTaskDelayUntil(&nextWake, 1);   // 1 kHz pacing

        if (!mDbReady) { nextWake = xTaskGetTickCount(); continue; }

        uint8_t rec[RECORD_SIZE];
        serializeEcgRecord(atsGetTime(), ecgPhase, rec);
        if (++ecgPhase >= ECG_LUT_LEN) ecgPhase = 0;

        // Enqueue into the deep ring. Block mode = wait if full (zero loss); the
        // wait only triggers when the writer has fallen >ring-depth behind (a very
        // long SD stall) and backpressures the sampler. Bounded so a dead writer
        // can't wedge sampling forever — those rare cases count as qfull.
        if (xQueueSend(mEcgQueue, rec, pdMS_TO_TICKS(2000)) != pdTRUE) {
            mEcgQueueDrops++;
            nextWake = xTaskGetTickCount();   // resync after a long block
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
    // Medical mode for the ECG stream: zero-loss backpressure + a dedicated
    // double-buffer on the primary channel (0). When an SD write stalls, records
    // back up into the alternate buffer and append blocks-then-catches-up instead
    // of dropping (was Drop + single slowBuf → lost 1–8% of samples under load).
    cfg.overflow = arcana::ats::OverflowPolicy::Block;
    cfg.primaryChannel = 0;            // ADS1298 ECG channel
    cfg.primaryBufA = sPrimaryBufA;
    cfg.primaryBufB = sPrimaryBufB;
    cfg.slowBuf = sSlowBuf;
    cfg.readCache = sReadCache;

    if (!mDb.open("sensor.ats", cfg)) {
        ESP_LOGW(TAG, "sensor.ats open failed, recreating");
        fsRemove("sensor.ats");
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

    // Fold the completing day into the lifetime total and persist it BEFORE the
    // rename. The `lastDay > mLifetimeLastDay` guard makes this idempotent; if a
    // crash happens after this point but before the rename, the next boot detects
    // the already-folded sensor.ats (day <= mLifetimeLastDay) and completes the
    // rename rather than double-counting. (Fold before close: mDb stats valid.)
    if (mLifetimeReady && lastDay > mLifetimeLastDay) {
        mLifetimeRecords += mDb.getStats().totalRecords;
        mLifetimeLastDay = lastDay;
        saveLifetime();
        ESP_LOGI(TAG, "lifetime folded day %lu -> %llu",
                 (unsigned long)lastDay, (unsigned long long)mLifetimeRecords);
    }

    mDb.close();
    mDbReady = false;

    char newName[20];
    snprintf(newName, sizeof(newName), "%08lu.ats", (unsigned long)lastDay);
    fsRename("sensor.ats", newName);

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
    arcana::ats::ArcanaTsSchema recStat = arcana::ats::ArcanaTsSchema::recordStat();
    mDeviceDb.addChannel(RECSTAT_CHANNEL, recStat);
    return mDeviceDb.start();
}

bool AtsStorageServiceImpl::openDeviceDbSafe(const arcana::ats::AtsConfig& cfg) {
    if (mDeviceDb.open("device.ats", cfg)) {
        // Live upgrade: add missing channels to an older device.ats.
        if (mDeviceDb.getChannelCount() > 0 && mDeviceDb.getChannelCount() < 3) {
            arcana::ats::ArcanaTsSchema creds = arcana::ats::ArcanaTsSchema::credentials();
            if (mDeviceDb.addChannelLive(2, creds)) {
                ESP_LOGI(TAG, "device.ats upgraded: added CREDS channel");
            }
        }
        if (mDeviceDb.getChannelCount() > 0 && mDeviceDb.getChannelCount() <= RECSTAT_CHANNEL) {
            arcana::ats::ArcanaTsSchema recStat = arcana::ats::ArcanaTsSchema::recordStat();
            if (mDeviceDb.addChannelLive(RECSTAT_CHANNEL, recStat)) {
                ESP_LOGI(TAG, "device.ats upgraded: added RECSTAT channel");
            }
        }
        return true;
    }

    // Corrupt → recreate
    ESP_LOGW(TAG, "device.ats corrupt, recreating");
    fsRemove("device_old.ats");
    fsRename("device.ats", "device_old.ats");
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
// Lifetime ECG record counter (device.ats ch3)
// ---------------------------------------------------------------------------
uint32_t AtsStorageServiceImpl::epochToDay(uint32_t epoch) {
    if (epoch <= 1577836800u) return 0;  // before 2020-01-01 = unset/invalid clock
    time_t t = (time_t)epoch;
    struct tm tm;
    gmtime_r(&t, &tm);
    return (uint32_t)((tm.tm_year + 1900) * 10000 + (tm.tm_mon + 1) * 100 + tm.tm_mday);
}

bool AtsStorageServiceImpl::loadLifetime(uint64_t& lifetime, uint32_t& lastDay) {
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() <= RECSTAT_CHANNEL) return false;
    uint8_t buf[16] = {};
    if (mDeviceDb.queryLatest(RECSTAT_CHANNEL, buf, 1) == 0) return false;  // no record yet
    memcpy(&lifetime, buf + 4, 8);   // RECSTAT: ts(4), lifetime(8), lastDay(4)
    memcpy(&lastDay,  buf + 12, 4);
    return true;
}

bool AtsStorageServiceImpl::saveLifetime() {
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() <= RECSTAT_CHANNEL) return false;
    uint8_t rec[16] = {};
    uint32_t ts = atsGetTime();
    memcpy(rec, &ts, 4);
    memcpy(rec + 4, &mLifetimeRecords, 8);
    memcpy(rec + 12, &mLifetimeLastDay, 4);
    bool ok = mDeviceDb.append(RECSTAT_CHANNEL, rec);
    mDeviceDb.flush();
    return ok;
}

void AtsStorageServiceImpl::initLifetimeCounter() {
    uint64_t lt = 0;
    uint32_t ld = 0;
    if (loadLifetime(lt, ld)) {
        mLifetimeRecords = lt;
        mLifetimeLastDay = ld;
        ESP_LOGI(TAG, "lifetime counter loaded: %llu (thru day %lu)",
                 (unsigned long long)mLifetimeRecords, (unsigned long)mLifetimeLastDay);
    } else {
        // First boot with this firmware. Seed from the off-device historical count
        // ONLY if the original card's day-files are still present, so a formatted /
        // blank card correctly starts at 0 instead of the stale historical seed.
        char seedFile[24];
        snprintf(seedFile, sizeof(seedFile), "/%08lu.ats", (unsigned long)LIFETIME_SEED_LAST_DAY);
        bool histPresent = mExFatVol.exists(seedFile);
        mLifetimeRecords = histPresent ? LIFETIME_SEED : 0;
        mLifetimeLastDay = histPresent ? LIFETIME_SEED_LAST_DAY : 0;
        saveLifetime();
        ESP_LOGW(TAG, "lifetime SEEDED %s: %llu (thru day %lu)",
                 histPresent ? "(historical card)" : "(blank card)",
                 (unsigned long long)mLifetimeRecords, (unsigned long)mLifetimeLastDay);
    }

    // Complete an interrupted midnight rotation: if the open sensor.ats belongs to
    // a day already folded into the lifetime total, the rename did not finish — do
    // it now so those records are not double-counted (folded + current sensor.ats).
    uint32_t sensorDay = epochToDay(mDb.getStats().lastTimestamp);
    if (sensorDay != 0 && sensorDay <= mLifetimeLastDay) {
        ESP_LOGW(TAG, "completing interrupted rotation: day %lu already folded",
                 (unsigned long)sensorDay);
        mDb.close();
        mDbReady = false;
        char nm[24];
        snprintf(nm, sizeof(nm), "%08lu.ats", (unsigned long)sensorDay);
        if (!fsRename("sensor.ats", nm)) {
            ESP_LOGE(TAG, "rotation-complete rename failed");
        }
        openDailyDb();
    }

    mLifetimeReady = true;
    ESP_LOGI(TAG, "lifetime ECG grand total: %llu",
             (unsigned long long)lifetimeRecordCount());
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
// SD filesystem helpers (SdFat exFAT) — used by rotation / cleanup / listing.
// Paths are volume-relative names ("sensor.ats"); SdFat prepends the root.
// ---------------------------------------------------------------------------

bool AtsStorageServiceImpl::fsRename(const char* oldName, const char* newName) {
    char o[24], n[24];
    snprintf(o, sizeof(o), "/%s", oldName);
    snprintf(n, sizeof(n), "/%s", newName);
    mExFatVol.remove(n);          // exFAT rename fails if target exists
    return mExFatVol.rename(o, n);
}

bool AtsStorageServiceImpl::fsRemove(const char* name) {
    char p[24];
    snprintf(p, sizeof(p), "/%s", name);
    return mExFatVol.remove(p);
}

void AtsStorageServiceImpl::fsListAts(void (*cb)(void* ctx, const char* name), void* ctx) {
    ExFatFile root, entry;
    if (!root.openRoot(&mExFatVol)) return;
    char name[64];
    while (entry.openNext(&root, O_RDONLY)) {
        if (!entry.isHidden() && !entry.isSubDir()) {
            size_t n = entry.getName(name, sizeof(name));
            if (n >= 4 && strcmp(name + n - 4, ".ats") == 0) {
                cb(ctx, name);
            }
        }
        entry.close();
    }
    root.close();
}

// ---------------------------------------------------------------------------
// Upload support
// ---------------------------------------------------------------------------

uint8_t AtsStorageServiceImpl::listPendingUploads(PendingFile* out, uint8_t maxCount) {
    // Two phases to avoid interleaving directory iteration with DB reads on the
    // same exFAT volume: (1) collect candidate "YYYYMMDD.ats" names + dates while
    // iterating the dir, (2) after the dir is closed, filter by isDateUploaded().
    struct Collect {
        PendingFile* out;
        uint8_t maxCount;
        uint8_t count;
    } ctx { out, maxCount, 0 };

    fsListAts([](void* c, const char* name) {
        auto* k = static_cast<Collect*>(c);
        if (k->count >= k->maxCount) return;
        if (strlen(name) != 12) return;                 // "YYYYMMDD.ats"
        if (strcmp(name, "sensor.ats") == 0 || strcmp(name, "device.ats") == 0) return;
        uint32_t date = 0;
        for (int i = 0; i < 8; i++) {
            if (name[i] < '0' || name[i] > '9') return;
            date = date * 10 + (name[i] - '0');
        }
        if (date < 20200101) return;
        strncpy(k->out[k->count].name, name, 15);
        k->out[k->count].name[15] = '\0';
        k->out[k->count].size = 0;   // size filled lazily by the uploader
        k->out[k->count].date = date;
        k->count++;
    }, &ctx);

    // Phase 2: drop already-uploaded dates (compact in place).
    uint8_t count = 0;
    for (uint8_t i = 0; i < ctx.count; i++) {
        if (isDateUploaded(out[i].date)) continue;
        if (count != i) out[count] = out[i];
        count++;
    }
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

    uint8_t rec[268];
    uint16_t n = mDeviceDb.queryLatest(2, rec, 1);
    if (n == 0) return false;

    uint16_t dataLen = 264;   // CREDS record data field (was 232 + commKey:32)
    if (dataLen > bufSize) dataLen = bufSize;
    memcpy(outBuf, rec + 4, dataLen);
    outLen = dataLen;
    return true;
}

bool AtsStorageServiceImpl::saveCredentials(const uint8_t* data, uint16_t len) {
    ESP_LOGI(TAG, "saveCredentials: ready=%d channels=%u len=%u",
             mDeviceDbReady, mDeviceDb.getChannelCount(), len);
    if (!mDeviceDbReady || mDeviceDb.getChannelCount() < 3) return false;
    if (len > 264) return false;  // max data portion of credentials record

    uint8_t rec[268] = {};
    uint32_t ts = atsGetTime();
    memcpy(rec, &ts, 4);
    memcpy(rec + 4, data, len);

    mDeviceDb.append(2, rec);
    mDeviceDb.flush();
    return true;
}

} // namespace Arcana::Storage
