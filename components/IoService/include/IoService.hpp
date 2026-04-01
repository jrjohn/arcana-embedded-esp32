#pragma once

#include "esp_err.h"

namespace Arcana::Io {

class IoService {
public:
    virtual ~IoService() = default;

    virtual esp_err_t init_HAL() = 0;
    virtual esp_err_t init() = 0;
    virtual esp_err_t start() = 0;

    virtual bool isUploadRequested() const = 0;
    virtual void clearUploadRequest() = 0;

    virtual bool isCancelRequested() const = 0;
    virtual void clearCancelRequest() = 0;

    virtual bool isFormatRequested() const = 0;
    virtual void clearFormatRequest() = 0;

    virtual void armCancel() = 0;
    virtual void disarmCancel() = 0;

protected:
    IoService() = default;
};

} // namespace Arcana::Io
