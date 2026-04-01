#pragma once

#include <cstdint>

namespace Arcana::Upload {

/// Global upload progress (read by ViewModel, written by upload task)
struct UploadProgress {
    volatile uint8_t  currentFile = 0;   ///< 1-based, 0 = idle
    volatile uint8_t  totalFiles = 0;
    volatile uint32_t bytesSent = 0;
    volatile uint32_t totalBytes = 0;
};

/**
 * HTTPS file upload service.
 * Uploads .ats files to cloud server with Bearer token + Content-Range resume.
 */
/// Progress callback: (currentFile, totalFiles, bytesSent, totalBytes)
using UploadProgressCallback = void (*)(uint8_t, uint8_t, uint32_t, uint32_t, void* ctx);

class HttpUploadService {
public:
    virtual ~HttpUploadService() = default;

    /// Set progress callback (called from upload write loop)
    virtual void setProgressCallback(UploadProgressCallback cb, void* ctx) = 0;

    /// Upload all pending .ats files + device.ats. Returns count of files uploaded.
    virtual uint8_t uploadPendingFiles() = 0;

    /// Upload a single file. Returns true on success.
    virtual bool uploadFile(const char* filename, const char* deviceId,
                            const char* token) = 0;

    virtual const UploadProgress& progress() const = 0;

protected:
    HttpUploadService() = default;
};

} // namespace Arcana::Upload
