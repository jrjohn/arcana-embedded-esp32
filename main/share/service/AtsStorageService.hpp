#pragma once

#include "Observable.hpp"
#include "SensorTypes.hpp"
#include "StorageTypes.hpp"  // from ObservableSensor (shared types)
#include "IoService.hpp"
#include "esp_err.h"
#include <cstdint>

class ExFatVolume;                  // SdFat (components/SdFat)
namespace arcana::ats { class IMutex; class IFilePort; }

namespace Arcana::Storage {

class AtsStorageService {
public:
    struct Input {
        Observable<Sensor::SensorData>* SensorDataEvents = nullptr;
        Io::IoService* Io = nullptr;   // upload / format requests (wired in AppContainer)
    };

    struct PendingFile {
        char name[16];      ///< "YYYYMMDD.ats"
        uint32_t size;
        uint32_t date;      ///< YYYYMMDD
    };
    static const uint8_t MAX_PENDING = 8;

    struct Output {
        Observable<StorageStats>* StatsEvents = nullptr;
    };

    Input input;
    Output output;

    virtual ~AtsStorageService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;
    virtual void stop() = 0;

    /// Query sensor records for a given day (YYYYMMDD).
    /// Returns number of records written to out.
    virtual uint16_t queryByDate(uint32_t dateYYYYMMDD,
                                 Sensor::SensorData* out,
                                 uint16_t maxCount) = 0;

    // Upload bookkeeping (used by HttpUploadService)
    virtual uint8_t listPendingUploads(PendingFile* out, uint8_t maxCount) = 0;
    virtual bool isDateUploaded(uint32_t dateYYYYMMDD) = 0;
    virtual void markUploaded(uint32_t dateYYYYMMDD) = 0;

    virtual bool isReady() const = 0;
    virtual void pauseRecording() = 0;
    virtual void resumeRecording() = 0;

    // Credential blob on device.ats (used by RegistrationService)
    virtual bool loadCredentials(uint8_t* outBuf, uint16_t bufSize, uint16_t& outLen) = 0;
    virtual bool saveCredentials(const uint8_t* data, uint16_t len) = 0;

    /// Shared exFAT volume + SD mutex. Every SD op from another service must hold sdMutex().
    virtual ExFatVolume* exfatVolume() = 0;
    virtual arcana::ats::IMutex* sdMutex() = 0;
    virtual arcana::ats::IFilePort* uploadReadPort() = 0;

protected:
    AtsStorageService() = default;
};

} // namespace Arcana::Storage
