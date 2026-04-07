#pragma once
#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, fmt, ...) do { (void)(tag); } while (0)

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

#ifdef __cplusplus
extern "C" {
#endif
inline void esp_log_level_set(const char*, esp_log_level_t) {}
#ifdef __cplusplus
}
#endif
