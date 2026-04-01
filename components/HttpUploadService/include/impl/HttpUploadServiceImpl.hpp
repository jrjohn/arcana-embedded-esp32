#pragma once

#include "HttpUploadService.hpp"

namespace Arcana::Upload {

class HttpUploadServiceImpl : public HttpUploadService {
public:
    static HttpUploadServiceImpl& getInstance();

    void setProgressCallback(UploadProgressCallback cb, void* ctx) override {
        mProgressCb = cb;
        mProgressCtx = ctx;
    }
    uint8_t uploadPendingFiles() override;
    bool uploadFile(const char* filename, const char* deviceId,
                    const char* token) override;
    const UploadProgress& progress() const override { return mProgress; }

private:
    HttpUploadServiceImpl() = default;

    uint32_t queryServerOffset(const char* filename, const char* deviceId);

    UploadProgress mProgress;
    UploadProgressCallback mProgressCb = nullptr;
    void* mProgressCtx = nullptr;

    void notifyProgress() {
        if (mProgressCb) {
            mProgressCb(mProgress.currentFile, mProgress.totalFiles,
                        mProgress.bytesSent, mProgress.totalBytes, mProgressCtx);
        }
    }
};

} // namespace Arcana::Upload
