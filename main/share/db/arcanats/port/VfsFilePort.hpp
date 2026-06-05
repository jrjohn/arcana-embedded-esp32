/**
 * @file VfsFilePort.hpp
 * @brief IFilePort implementation using ESP-IDF VFS (POSIX fopen/fread/fwrite)
 *
 * After mounting FatFS via esp_vfs_fat_sdspi_mount(), standard POSIX
 * file I/O works transparently through the VFS layer.
 */

#ifndef ARCANA_VFS_FILE_PORT_HPP
#define ARCANA_VFS_FILE_PORT_HPP

#include "ats/IFilePort.hpp"
#include "ats/ArcanaTsTypes.hpp"
#include <cstdio>
#include <cstring>

namespace arcana {
namespace ats {

class VfsFilePort : public IFilePort {
public:
    /**
     * @param mountPoint  VFS mount prefix, e.g. "/sdcard"
     */
    explicit VfsFilePort(const char* mountPoint = "/sdcard")
        : mFp(nullptr)
    {
        strncpy(mMount, mountPoint, sizeof(mMount) - 1);
        mMount[sizeof(mMount) - 1] = '\0';
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
    bool isOpen() const override { return mFp != nullptr; }

private:
    FILE* mFp;
    char mMount[32];

    /** Build full path: "{mount}/{path}" */
    void buildPath(const char* path, char* out, size_t outSize) const {
        snprintf(out, outSize, "%s/%s", mMount, path);
    }
};

} // namespace ats
} // namespace arcana

#endif /* ARCANA_VFS_FILE_PORT_HPP */
