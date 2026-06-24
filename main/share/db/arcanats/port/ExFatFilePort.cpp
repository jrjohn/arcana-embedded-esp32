/**
 * @file ExFatFilePort.cpp
 * @brief IFilePort → SdFat ExFatLib. 64-bit offsets so a single .ats is no
 *        longer capped at 4GB (FAT32) — exFAT supports far larger single files.
 *
 * Mirrors FatFsFilePort's behaviour 1:1 (retries, seek zero-fill extend,
 * truncate-at-position) but on SdFat's ExFatFile/ExFatVolume instead of raw
 * FatFs. The SD is mounted by AtsStorageServiceImpl (sdspi card init + SdFat
 * exFAT); this port just opens files on that shared volume.
 */

#include "ExFatFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstring>

namespace arcana {
namespace ats {

static const char* TAG = "ExFatFilePort";
static const int MAX_RETRIES = 3;

bool ExFatFilePort::open(const char* path, uint8_t mode) {
    if (mIsOpen || !mVol) return false;

    buildPath(path, mPath, sizeof(mPath));

    // mode → SdFat oflag. Access bits first, then create/truncate.
    oflag_t oflag;
    if ((mode & ATS_MODE_RW) == ATS_MODE_RW) oflag = O_RDWR;
    else if (mode & ATS_MODE_WRITE)          oflag = O_WRONLY;
    else                                     oflag = O_RDONLY;
    if (mode & ATS_MODE_CREATE) oflag |= O_CREAT | O_TRUNC;  // == FatFs FA_CREATE_ALWAYS

    // open-existing without create: a missing file is expected (ArcanaTsDb probes
    // before it creates) — fail quietly, like FatFsFilePort's FR_NO_FILE path.
    if (!(mode & ATS_MODE_CREATE) && !mVol->exists(mPath)) {
        return false;
    }

    if (mFile.open(mVol, mPath, oflag)) {
        mIsOpen = true;
        return true;
    }
    // LCOV_EXCL_START — defensive: the entry exists() but open() failed. SdFat
    // cannot produce this for a normal file (a present directory entry opens);
    // kept as a guard, validated via HIL.
    ESP_LOGW(TAG, "open(%s) failed", mPath);
    return false;
    // LCOV_EXCL_STOP
}

bool ExFatFilePort::close() {
    if (!mIsOpen) return false;
    bool ok = mFile.close();
    mIsOpen = false;
    return ok;
}

int32_t ExFatFilePort::read(uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        int n = mFile.read(buf, size);
        if (n >= 0) return static_cast<int32_t>(n);
        vTaskDelay(1);
    }
    ESP_LOGE(TAG, "read failed after %d retries", MAX_RETRIES);
    return -1;
}

int32_t ExFatFilePort::write(const uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;
    size_t lastWr = 0;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        lastWr = mFile.write(buf, size);
        if (lastWr == size) return static_cast<int32_t>(lastWr);
        vTaskDelay(1);
    }
    ESP_LOGE(TAG, "write failed: wr=%u/%u", (unsigned)lastWr, (unsigned)size);
    return -1;
}

bool ExFatFilePort::seek(uint64_t offset) {
    if (!mIsOpen) return false;

    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (mFile.seekSet(offset)) return true;
        vTaskDelay(1);
    }

    // seek beyond EOF failed: seek to EOF and zero-fill out to the target offset
    // (mirrors FatFsFilePort — handles a sparse/damaged extend).
    uint64_t curSize = mFile.fileSize();
    if (offset >= curSize) {
        if (!mFile.seekSet(curSize)) {   // LCOV_EXCL_LINE — seekSet to EOF (a valid offset) cannot fail for a normal file
            ESP_LOGE(TAG, "seekSet(EOF) failed");  // LCOV_EXCL_LINE
            return false;                          // LCOV_EXCL_LINE
        }
        uint8_t zeros[64];
        memset(zeros, 0, sizeof(zeros));
        uint64_t remaining = offset - curSize;
        while (remaining > 0) {
            size_t chunk = (remaining > sizeof(zeros)) ? sizeof(zeros) : (size_t)remaining;
            if (mFile.write(zeros, chunk) != chunk) {
                ESP_LOGE(TAG, "zero-fill extend to %llu failed", (unsigned long long)offset);
                return false;
            }
            remaining -= chunk;
        }
        return true;  // position now at target offset
    }

    // LCOV_EXCL_START — defensive: seekSet to an in-range offset cannot fail for
    // a normal contiguous exFAT file (no failing I/O); kept as a guard, HIL-validated.
    ESP_LOGE(TAG, "seekSet(%llu) failed", (unsigned long long)offset);
    return false;
    // LCOV_EXCL_STOP
}

bool ExFatFilePort::sync() {
    if (!mIsOpen) return false;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (mFile.sync()) return true;
        vTaskDelay(1);
    }
    return false;
}

uint64_t ExFatFilePort::tell() {
    if (!mIsOpen) return 0;
    return mFile.curPosition();
}

uint64_t ExFatFilePort::size() {
    if (!mIsOpen) return 0;
    return mFile.fileSize();
}

bool ExFatFilePort::truncate() {
    if (!mIsOpen) return false;
    // exFAT has a single allocation structure (no FatFs n_fats==2 TexFAT case):
    // truncate at the current position directly.
    return mFile.truncate();
}

} // namespace ats
} // namespace arcana
