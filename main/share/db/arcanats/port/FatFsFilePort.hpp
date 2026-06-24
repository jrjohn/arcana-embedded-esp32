/**
 * @file FatFsFilePort.hpp
 * @brief IFilePort implementation using RAW FatFs (ff.h) — 64-bit file offsets.
 *
 * Ported from the STM32 FatFsFilePort. Unlike VfsFilePort (stdio fopen/fseek,
 * whose signed-32-bit `long` offset caps every file at 2GB on ESP32), this port
 * goes straight to the FatFs API: f_lseek takes a 64-bit FSIZE_t, so with exFAT
 * enabled (FF_FS_EXFAT=1) a single .ats file can exceed 4GB — matching STM32.
 *
 * Coexists with esp_vfs_fat: esp_vfs_fat_sdspi_mount() registers the SD card on
 * a FatFs drive (and keeps the "/sdcard" VFS path working for the upload /
 * housekeeping code). This port opens the SAME drive via raw f_open, using the
 * drive prefix (default "" = the default volume, normally drive 0) rather than
 * the VFS prefix. Do NOT f_mount here — esp_vfs_fat already did.
 */

#ifndef ARCANA_FATFS_FILE_PORT_HPP
#define ARCANA_FATFS_FILE_PORT_HPP

#include "ats/IFilePort.hpp"
#include "ff.h"
#include <cstdio>
#include <cstring>

namespace arcana {
namespace ats {

class FatFsFilePort : public IFilePort {
public:
    /**
     * @param drive  FatFs drive prefix for the volume esp_vfs_fat mounted.
     *               "" → default volume (drive 0, the usual single-SD case);
     *               "0:" → explicit drive 0. The DB passes relative names
     *               ("sensor.ats") which are resolved to "{drive}/{name}".
     */
    explicit FatFsFilePort(const char* drive = "")
        : mIsOpen(false), mFaMode(0)
    {
        strncpy(mDrive, drive, sizeof(mDrive) - 1);
        mDrive[sizeof(mDrive) - 1] = '\0';
        mPath[0] = '\0';
    }

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
    FIL  mFil;
    bool mIsOpen;
    BYTE mFaMode;       // saved FatFs open flags (parity with STM32)
    char mDrive[8];     // drive prefix, e.g. "" or "0:"
    char mPath[40];     // built raw path, e.g. "0:/sensor.ats"

    /** Build the raw FatFs path: "{drive}/{name}" (or "/{name}" on default vol). */
    void buildPath(const char* name, char* out, size_t outSize) const {
        if (mDrive[0] != '\0') {
            snprintf(out, outSize, "%s/%s", mDrive, name);
        } else {
            snprintf(out, outSize, "/%s", name);
        }
    }
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_FATFS_FILE_PORT_HPP */
