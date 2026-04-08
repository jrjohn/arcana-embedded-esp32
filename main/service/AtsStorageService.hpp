#pragma once

#include "Observable.hpp"
#include "SensorTypes.hpp"
#include "StorageTypes.hpp"  // from ObservableSensor (shared types)
#include "esp_err.h"

namespace Arcana::Storage {

class AtsStorageService {
public:
    struct Input {
        Observable<Sensor::SensorData>* SensorDataEvents = nullptr;
    };

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

protected:
    AtsStorageService() = default;
};

} // namespace Arcana::Storage
