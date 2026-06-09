/**
 * @file FatFsFilePort.cpp
 * @brief IFilePort → raw FatFs (ff.h). 64-bit offsets (f_lseek FSIZE_t) so a
 *        single .ats file is no longer capped at 2GB like the stdio VfsFilePort.
 *
 * Ported from STM32 FatFsFilePort.cpp. Differences:
 *   - paths go through buildPath() (esp_vfs_fat drive prefix), since the SD is
 *     mounted by esp_vfs_fat, not a bare f_mount("") as on STM32;
 *   - recovery: STM32's sdio_force_reinit() has no direct ESP32 SDSPI analogue
 *     here, so we retry with a short delay and surface a hard error to the
 *     storage service (which owns remount). FIL.err is cleared before each op.
 */

#include "FatFsFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cstring>

namespace arcana {
namespace ats {

static const char* TAG = "FatFsFilePort";
static const int MAX_RETRIES = 3;

bool FatFsFilePort::open(const char* path, uint8_t mode) {
    if (mIsOpen) return false;
    memset(&mFil, 0, sizeof(mFil));

    buildPath(path, mPath, sizeof(mPath));

    BYTE fa = 0;
    if (mode & ATS_MODE_READ)   fa |= FA_READ;
    if (mode & ATS_MODE_WRITE)  fa |= FA_WRITE;
    if (mode & ATS_MODE_CREATE) fa |= FA_CREATE_ALWAYS;
    else                        fa |= FA_OPEN_EXISTING;

    FRESULT fr = f_open(&mFil, mPath, fa);
    if (fr == FR_OK) {
        mIsOpen = true;
        mFaMode = fa;
        return true;
    }
    // FR_NO_FILE is expected when ArcanaTsDb probes open-existing before create.
    if (fr != FR_NO_FILE) {
        ESP_LOGW(TAG, "f_open(%s) failed: %d", mPath, (int)fr);
    }
    return false;
}

bool FatFsFilePort::close() {
    if (!mIsOpen) return false;
    FRESULT fr = f_close(&mFil);
    mIsOpen = false;
    return fr == FR_OK;
}

int32_t FatFsFilePort::read(uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        UINT bytesRead = 0;
        FRESULT fr = f_read(&mFil, buf, size, &bytesRead);
        if (fr == FR_OK) {
            return static_cast<int32_t>(bytesRead);
        }
        vTaskDelay(1);
    }
    ESP_LOGE(TAG, "f_read failed after %d retries", MAX_RETRIES);
    return -1;
}

int32_t FatFsFilePort::write(const uint8_t* buf, uint32_t size) {
    if (!mIsOpen) return -1;
    FRESULT lastErr = FR_OK;
    UINT lastWr = 0;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        lastWr = 0;
        lastErr = f_write(&mFil, buf, size, &lastWr);
        if (lastErr == FR_OK && lastWr == size) {
            return static_cast<int32_t>(lastWr);
        }
        vTaskDelay(1);
    }
    ESP_LOGE(TAG, "f_write failed: fr=%d wr=%u/%u", (int)lastErr, (unsigned)lastWr, (unsigned)size);
    return -1;
}

bool FatFsFilePort::seek(uint64_t offset) {
    if (!mIsOpen) return false;

    FRESULT lastErr = FR_OK;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        lastErr = f_lseek(&mFil, (FSIZE_t)offset);   // FSIZE_t is 64-bit under exFAT
        if (lastErr == FR_OK) return true;
        vTaskDelay(1);
    }

    // f_lseek beyond EOF failed: seek to EOF and zero-fill out to the target
    // offset (mirrors STM32 — handles a damaged cluster chain / sparse extend).
    uint64_t curSize = f_size(&mFil);
    if (offset >= curSize) {
        mFil.err = 0;
        if (f_lseek(&mFil, (FSIZE_t)curSize) != FR_OK) {
            ESP_LOGE(TAG, "f_lseek(EOF) failed: %d", (int)lastErr);
            return false;
        }
        uint8_t zeros[64];
        memset(zeros, 0, sizeof(zeros));
        uint64_t remaining = offset - curSize;
        while (remaining > 0) {
            UINT chunk = (remaining > sizeof(zeros)) ? (UINT)sizeof(zeros) : (UINT)remaining;
            UINT written = 0;
            mFil.err = 0;
            if (f_write(&mFil, zeros, chunk, &written) != FR_OK || written != chunk) {
                ESP_LOGE(TAG, "zero-fill extend to %llu failed", (unsigned long long)offset);
                return false;
            }
            remaining -= written;
        }
        return true;  // fptr now at target offset
    }

    ESP_LOGE(TAG, "f_lseek(%llu) failed: %d", (unsigned long long)offset, (int)lastErr);
    return false;
}

bool FatFsFilePort::sync() {
    if (!mIsOpen) return false;
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        mFil.err = 0;
        if (f_sync(&mFil) == FR_OK) return true;
        vTaskDelay(1);
    }
    return false;
}

uint64_t FatFsFilePort::tell() {
    if (!mIsOpen) return 0;
    return (uint64_t)f_tell(&mFil);
}

uint64_t FatFsFilePort::size() {
    if (!mIsOpen) return 0;
    return (uint64_t)f_size(&mFil);
}

bool FatFsFilePort::truncate() {
    if (!mIsOpen) return false;
    mFil.err = 0;
    // n_fats==2 (TexFAT): the committed FAT has a correct chain → safe to
    // truncate. Single FAT (n_fats==1): skip — a broken chain at the cut point
    // would make subsequent writes impossible (matches STM32).
    if (mFil.obj.fs->n_fats == 2) {
        return f_truncate(&mFil) == FR_OK;
    }
    return f_sync(&mFil) == FR_OK;
}

} // namespace ats
} // namespace arcana
