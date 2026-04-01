/**
 * @file SerialAppender.hpp
 * @brief Log appender — routes to ESP_LOG macros
 *
 * Format: [L][TAG] 0xCODE p=PARAM
 * All levels pass through (minLevel = Trace).
 */

#pragma once

#include "Log.hpp"
#include "esp_log.h"

namespace arcana {
namespace log {

class SerialAppender : public IAppender {
public:
    void append(const LogEvent& event) override {
        static const char* const SRC_TAG[] = {
            "SYS",  "SDIO", "SENS", "WiFi", "Pump",
            "Cryp", "ATS",  "NTP",  "MQTT", "BLE",
            "OTA",  "CMD",  "LCD",  "UPL",  "REG",
            "ESPFW",
        };
        static const uint8_t SRC_COUNT = sizeof(SRC_TAG) / sizeof(SRC_TAG[0]);

        const char* src = (event.source < SRC_COUNT)
                         ? SRC_TAG[event.source] : "???";

        esp_log_level_t espLevel;
        switch (static_cast<Level>(event.level)) {
            case Level::Trace: case Level::Debug: espLevel = ESP_LOG_DEBUG; break;
            case Level::Info:  espLevel = ESP_LOG_INFO; break;
            case Level::Warn:  espLevel = ESP_LOG_WARN; break;
            case Level::Error: espLevel = ESP_LOG_ERROR; break;
            case Level::Fatal: espLevel = ESP_LOG_ERROR; break;
            default: espLevel = ESP_LOG_INFO; break;
        }

        if (event.param != 0) {
            ESP_LOG_LEVEL(espLevel, src, "0x%04X p=%lu",
                          (unsigned)event.code, (unsigned long)event.param);
        } else {
            ESP_LOG_LEVEL(espLevel, src, "0x%04X", (unsigned)event.code);
        }
    }

    Level minLevel() const override { return Level::Trace; }
};

} // namespace log
} // namespace arcana
