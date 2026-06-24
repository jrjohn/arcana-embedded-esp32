/**
 * @file EspIdfBlockDev.hpp
 * @brief SdFat block device backed by ESP-IDF's sdmmc sector I/O.
 *
 * Bridges SdFat's ExFatLib to the SD card. ESP-IDF's own FatFs exFAT is broken
 * on this board's SPI host, and SdFat's own SD-card init can't bring the card up
 * over a thin external-SPI shim — but ESP-IDF's sdspi_host CAN. So we let ESP-IDF
 * init the card (sdmmc_card_init → sdmmc_card_t*) and hand SdFat ONLY the block
 * I/O through this interface. Spike-proven: format/mount/write/read all pass with
 * fatType=exFAT. See FatFsFilePort/ExFatFilePort for the file-level layer above.
 *
 * NOTE: not internally locked — all access (via ExFatVolume/ExFatFile) must be
 * serialized by the storage service's SD mutex.
 */

#ifndef ARCANA_ATS_ESPIDF_BLOCKDEV_HPP
#define ARCANA_ATS_ESPIDF_BLOCKDEV_HPP

#include "arduino_compat.h"   // __FlashStringHelper / SS / millis... must precede SdFat.h
#include "SdFat.h"
#include "sdmmc_cmd.h"
#include "esp_err.h"

namespace arcana {
namespace ats {

class EspIdfBlockDev : public FsBlockDeviceInterface {
public:
    void setCard(sdmmc_card_t* c) { mCard = c; }

    bool isBusy() override { return false; }

    bool readSector(Sector_t s, uint8_t* dst) override { return readSectors(s, dst, 1); }
    bool readSectors(Sector_t s, uint8_t* dst, size_t ns) override {
        return mCard && sdmmc_read_sectors(mCard, dst, s, ns) == ESP_OK;
    }

    Sector_t sectorCount() override { return mCard ? (Sector_t)mCard->csd.capacity : 0; }
    bool syncDevice() override { return true; }

    bool writeSector(Sector_t s, const uint8_t* src) override { return writeSectors(s, src, 1); }
    bool writeSectors(Sector_t s, const uint8_t* src, size_t ns) override {
        return mCard && sdmmc_write_sectors(mCard, src, s, ns) == ESP_OK;
    }

private:
    sdmmc_card_t* mCard = nullptr;
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_ATS_ESPIDF_BLOCKDEV_HPP */
