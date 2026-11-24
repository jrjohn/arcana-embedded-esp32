#include "impl/FreezeGuardServiceImpl.hpp"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <cstdio>

static const char* TAG = "FreezeGuard";

#ifdef CONFIG_ESP_TASK_WDT_PANIC
static constexpr int kPanic = 1;
#else
static constexpr int kPanic = 0;
#endif

#ifndef CONFIG_ESP_TASK_WDT_TIMEOUT_S
#define CONFIG_ESP_TASK_WDT_TIMEOUT_S 0
#endif

namespace Arcana::Freeze {

namespace {
struct Breadcrumb {
    uint32_t magic;
    uint32_t stage;
    uint32_t uptimeS;
};
RTC_NOINIT_ATTR Breadcrumb sCrumb;

ResetKind mapReset(esp_reset_reason_t r) {
    switch (r) {
    case ESP_RST_POWERON:  return ResetKind::PowerOn;
    case ESP_RST_SW:       return ResetKind::Software;
    case ESP_RST_TASK_WDT: return ResetKind::TaskWdt;
    case ESP_RST_INT_WDT:  return ResetKind::IntWdt;
    case ESP_RST_WDT:      return ResetKind::OtherWdt;
    case ESP_RST_PANIC:    return ResetKind::Panic;
    case ESP_RST_BROWNOUT: return ResetKind::Brownout;
    default:               return ResetKind::Other;
    }
}

void record(Stage stage) {
    sCrumb.stage = static_cast<uint32_t>(stage);
    sCrumb.uptimeS = static_cast<uint32_t>(esp_timer_get_time() / 1000000);
}
} // namespace

FreezeGuardService& FreezeGuardServiceImpl::getInstance() {
    static FreezeGuardServiceImpl sInstance;
    return sInstance;
}

esp_err_t FreezeGuardServiceImpl::init_HAL() {
    const ResetKind kind = mapReset(esp_reset_reason());
    char line[96];
    if (formatFreezeLine(kind, sCrumb.magic == kBreadcrumbMagic, sCrumb.stage, sCrumb.uptimeS,
                         line, sizeof line)) {
        printf("%s\n", line);   // one plain line for host tools (tools/flash-safe.py)
    }
    ESP_LOGI(TAG, "reset=%s", resetKindName(kind));
    sCrumb.magic = kBreadcrumbMagic;
    record(Stage::Boot);
    return ESP_OK;
}

esp_err_t FreezeGuardServiceImpl::init() { return ESP_OK; }
esp_err_t FreezeGuardServiceImpl::start() { return ESP_OK; }

void FreezeGuardServiceImpl::stop() {
    if (mWatching.exchange(false)) esp_task_wdt_delete(nullptr);
}

int FreezeGuardServiceImpl::watchdogTimeoutS() const { return CONFIG_ESP_TASK_WDT_TIMEOUT_S; }

esp_err_t FreezeGuardServiceImpl::watchCurrentTask() {
    const esp_err_t e = esp_task_wdt_add(nullptr);
    mWatching.store(e == ESP_OK);
    ESP_LOGI(TAG, "FREEZE GUARD: task on TWDT, timeout=%ds panic=%d -> %s",
             CONFIG_ESP_TASK_WDT_TIMEOUT_S, kPanic, esp_err_to_name(e));
    return e;
}

void FreezeGuardServiceImpl::feed(Stage stage) {
    record(stage);
    if (mWatching.load()) esp_task_wdt_reset();
}

void FreezeGuardServiceImpl::pause(const char* reason) {
    record(Stage::Paused);
    if (mWatching.exchange(false)) {
        esp_task_wdt_delete(nullptr);
        ESP_LOGI(TAG, "paused: %s", reason ? reason : "?");
    }
}

void FreezeGuardServiceImpl::resume() {
    if (!mWatching.load()) {
        mWatching.store(esp_task_wdt_add(nullptr) == ESP_OK);
    }
    record(Stage::LoopWork);
}

} // namespace Arcana::Freeze
