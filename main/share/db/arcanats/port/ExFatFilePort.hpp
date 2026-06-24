/**
 * @file ExFatFilePort.hpp
 * @brief IFilePort implementation over SdFat's ExFatLib (exFAT, 64-bit offsets).
 *
 * Replaces FatFsFilePort: instead of ESP-IDF's FatFs (whose exFAT is broken on
 * this board's SPI host), the SD is formatted/mounted as exFAT by SdFat, fed by
 * EspIdfBlockDev over ESP-IDF's sdmmc sector I/O. A single .ats file can exceed
 * 4GB. Mirrors FatFsFilePort's robustness: read/write retries, seek-beyond-EOF
 * zero-fill extend, truncate-at-position.
 *
 * Holds a pointer to the shared ExFatVolume owned by AtsStorageServiceImpl.
 * NOT internally locked — the storage service serializes all SD access (the
 * ECG writer task and the upload task share one volume) via its SD mutex.
 */

#ifndef ARCANA_EXFAT_FILE_PORT_HPP
#define ARCANA_EXFAT_FILE_PORT_HPP

#include "ats/IFilePort.hpp"
#include "arduino_compat.h"   // __FlashStringHelper / SS / millis... must precede SdFat.h
#include "SdFat.h"
#include <cstdio>

namespace arcana {
namespace ats {

class ExFatFilePort : public IFilePort {
public:
    /** @param vol shared exFAT volume (owned by the storage service). May be
     *  bound later via setVolume() — the service constructs ports before mount. */
    explicit ExFatFilePort(ExFatVolume* vol = nullptr)
        : mVol(vol), mIsOpen(false) {
        mPath[0] = '\0';
    }

    void setVolume(ExFatVolume* vol) { mVol = vol; }

    bool open(const char* path, uint8_t mode) override;
    bool close() override;
    int32_t read(uint8_t* buf, uint32_t size) override;
    int32_t write(const uint8_t* buf, uint32_t size) override;
    bool seek(uint64_t offset) override;
    bool sync() override;
    uint64_t tell() override;
    uint64_t size() override;
    bool truncate() override;
    bool isOpen() const override { return mIsOpen; }

private:
    ExFatVolume* mVol;
    ExFatFile    mFile;
    bool         mIsOpen;
    char         mPath[40];   // built path, e.g. "/sensor.ats"

    /** Build an absolute volume path: "sensor.ats" → "/sensor.ats". */
    void buildPath(const char* name, char* out, size_t outSize) const {
        snprintf(out, outSize, "/%s", name);
    }
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_EXFAT_FILE_PORT_HPP */
