#pragma once

#include "HttpUploadService.hpp"

namespace Arcana::Upload {

class HttpUploadServiceImpl : public HttpUploadService {
public:
    static HttpUploadServiceImpl& getInstance();

    uint8_t uploadPendingFiles() override;
    bool uploadFile(const char* filename, const char* deviceId,
                    const char* token) override;
    const UploadProgress& progress() const override { return mProgress; }

private:
    HttpUploadServiceImpl() = default;

    uint32_t queryServerOffset(const char* filename, const char* deviceId);

    UploadProgress mProgress;
};

} // namespace Arcana::Upload
