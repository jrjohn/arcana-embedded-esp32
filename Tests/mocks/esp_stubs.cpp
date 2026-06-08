// Stubs for ESP-IDF + FreeRTOS APIs used by ESP32 components.
// All operations are no-ops or return success — sufficient for header-only
// platform-independent code (FrameCodec, CommandCodec, Observable, etc).

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <setjmp.h>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_random.h"

// ────────────────────────────────────────────────────────────────────────────
// Mutex stubs (use placeholder pointer; reentrant fine for single-thread tests)
// ────────────────────────────────────────────────────────────────────────────
// Test injection: when set to a non-zero count N, the next N calls return
// nullptr to simulate FreeRTOS heap exhaustion in mutex creation. Default 0
// = always succeed. Decremented after each forced failure.
int g_test_xSemaphoreCreateMutex_fail_count = 0;

extern "C" SemaphoreHandle_t xSemaphoreCreateMutex(void) {
    if (g_test_xSemaphoreCreateMutex_fail_count > 0) {
        g_test_xSemaphoreCreateMutex_fail_count--;
        return nullptr;
    }
    return (SemaphoreHandle_t)malloc(1);
}
extern "C" SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t* buf) {
    // Return a non-null pointer (use buf itself); FreeRtosMutex only checks
    // mHandle != nullptr.
    return (SemaphoreHandle_t)buf;
}
extern "C" SemaphoreHandle_t xSemaphoreCreateBinary(void) {
    return (SemaphoreHandle_t)malloc(1);
}
extern "C" BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t)  { return pdTRUE; }
extern "C" BaseType_t xSemaphoreGive(SemaphoreHandle_t)              { return pdTRUE; }
extern "C" void       vSemaphoreDelete(SemaphoreHandle_t s) { if (s) free(s); }

// ────────────────────────────────────────────────────────────────────────────
// Queue stubs — store-and-forward FIFO backed by std::vector
// (Observable<T> uses xQueueSend / xQueueReceive for async dispatch tests)
// ────────────────────────────────────────────────────────────────────────────
struct FakeQueue {
    UBaseType_t           length;
    UBaseType_t           item_size;
    std::vector<uint8_t>  data;
};

extern "C" QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size) {
    auto* q = new FakeQueue;
    q->length = length;
    q->item_size = item_size;
    return (QueueHandle_t)q;
}
extern "C" QueueHandle_t xQueueCreateStatic(UBaseType_t length, UBaseType_t item_size,
                                             uint8_t*, StaticQueue_t*) {
    return xQueueCreate(length, item_size);
}
extern "C" void vQueueDelete(QueueHandle_t q) {
    if (q) delete static_cast<FakeQueue*>(q);
}
extern "C" BaseType_t xQueueSendToBack(QueueHandle_t q, const void* item, TickType_t) {
    auto* fq = static_cast<FakeQueue*>(q);
    if (!fq || !item) return pdFALSE;
    if (fq->data.size() / fq->item_size >= fq->length) return pdFALSE;
    const uint8_t* src = static_cast<const uint8_t*>(item);
    fq->data.insert(fq->data.end(), src, src + fq->item_size);
    return pdTRUE;
}
extern "C" BaseType_t xQueueSendToBackFromISR(QueueHandle_t q, const void* item, BaseType_t* hp) {
    if (hp) *hp = pdFALSE;
    return xQueueSendToBack(q, item, 0);
}
// Test escape: tests can install a sigjmp_buf so xQueueReceive longjmps out
// after N calls. Used to drive infinite-loop task bodies in unit tests.
sigjmp_buf g_test_xqueue_escape_buf;
int g_test_xqueue_escape_after = -1;
int g_test_xqueue_receive_calls = 0;

extern "C" BaseType_t xQueueReceive(QueueHandle_t q, void* out, TickType_t) {
    g_test_xqueue_receive_calls++;
    if (g_test_xqueue_escape_after >= 0 &&
        g_test_xqueue_receive_calls > g_test_xqueue_escape_after) {
        siglongjmp(g_test_xqueue_escape_buf, 1);
    }
    auto* fq = static_cast<FakeQueue*>(q);
    if (!fq || !out || fq->data.empty()) return pdFALSE;
    memcpy(out, fq->data.data(), fq->item_size);
    fq->data.erase(fq->data.begin(), fq->data.begin() + fq->item_size);
    return pdTRUE;
}
extern "C" UBaseType_t uxQueueSpacesAvailable(QueueHandle_t q) {
    auto* fq = static_cast<FakeQueue*>(q);
    if (!fq) return 0;
    return fq->length - (fq->data.size() / fq->item_size);
}
extern "C" UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q) {
    auto* fq = static_cast<FakeQueue*>(q);
    if (!fq) return 0;
    return fq->data.size() / fq->item_size;
}

// ────────────────────────────────────────────────────────────────────────────
// Task stubs — never actually create tasks; xTaskCreate returns pdFAIL so
// Observable code skips spawning tasks. Tests use synchronous Notify() instead.
// ────────────────────────────────────────────────────────────────────────────
extern "C" BaseType_t xTaskCreate(TaskFunction_t, const char*, uint32_t, void*,
                                   UBaseType_t, TaskHandle_t* out) {
    if (out) *out = (TaskHandle_t)0x1;  // non-null so Observable thinks task exists
    return pdPASS;
}
extern "C" BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t,
                                              void*, UBaseType_t, TaskHandle_t* out,
                                              BaseType_t /*coreId*/) {
    if (out) *out = (TaskHandle_t)0x1;
    return pdPASS;
}
extern "C" void       vTaskDelete(TaskHandle_t)        {}
extern "C" void       vTaskDelay(TickType_t)           {}

// Settable tick — tests that need a non-zero clock can set this directly.
TickType_t g_test_tick_count = 0;
extern "C" TickType_t xTaskGetTickCount(void)          { return g_test_tick_count; }

// Settable BLE name set result — used by SetDeviceNameCommand failure tests.
extern "C" {
esp_err_t g_bt_set_name_result = ESP_OK;
}
extern "C" BaseType_t xTaskNotifyGive(TaskHandle_t)    { return pdTRUE; }

// Test escape for ulTaskNotifyTake — used to drive infinite for(;;) loops
// in MainView::renderTaskFunc.
sigjmp_buf g_test_unotify_escape_buf;
int g_test_unotify_escape_after = -1;
int g_test_unotify_take_calls = 0;

extern "C" uint32_t ulTaskNotifyTake(BaseType_t, TickType_t) {
    g_test_unotify_take_calls++;
    if (g_test_unotify_escape_after >= 0 &&
        g_test_unotify_take_calls > g_test_unotify_escape_after) {
        siglongjmp(g_test_unotify_escape_buf, 1);
    }
    return 0;
}

// ────────────────────────────────────────────────────────────────────────────
// esp_timer stubs
// ────────────────────────────────────────────────────────────────────────────
extern "C" esp_err_t esp_timer_create(const esp_timer_create_args_t*, esp_timer_handle_t* out) {
    if (out) *out = (esp_timer_handle_t)0x1;
    return ESP_OK;
}
extern "C" esp_err_t esp_timer_start_periodic(esp_timer_handle_t, uint64_t) { return ESP_OK; }
extern "C" esp_err_t esp_timer_stop(esp_timer_handle_t)                     { return ESP_OK; }
extern "C" esp_err_t esp_timer_delete(esp_timer_handle_t)                   { return ESP_OK; }
extern "C" int64_t   esp_timer_get_time(void)                               { return 0; }

// ────────────────────────────────────────────────────────────────────────────
// esp_system / esp_mac stubs
// ────────────────────────────────────────────────────────────────────────────
extern "C" const char* esp_get_idf_version(void)        { return "v5.5.2-test"; }
extern "C" uint32_t    esp_get_free_heap_size(void)     { return 100000; }
extern "C" uint32_t    esp_get_minimum_free_heap_size(void) { return 50000; }
extern "C" void        esp_restart(void)                {}

extern "C" esp_err_t esp_read_mac(uint8_t* mac, esp_mac_type_t) {
    static const uint8_t fake[6] = {0xA4, 0xE5, 0x7C, 0xDA, 0x59, 0x2C};
    if (mac) memcpy(mac, fake, 6);
    return ESP_OK;
}
extern "C" esp_err_t esp_efuse_mac_get_default(uint8_t* mac) {
    return esp_read_mac(mac, ESP_MAC_BT);
}

// ────────────────────────────────────────────────────────────────────────────
// esp_random stubs — backed by libc rand() for reproducibility in tests.
// Production firmware uses the ESP32 hardware TRNG; here we just need
// non-zero bytes that won't trip mbedtls's "trivial RNG" rejection logic.
// ────────────────────────────────────────────────────────────────────────────
extern "C" uint32_t esp_random(void) {
    return static_cast<uint32_t>(rand());
}

extern "C" void esp_fill_random(void* buf, size_t len) {
    auto* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < len; ++i) {
        p[i] = static_cast<uint8_t>(rand() & 0xFF);
    }
}
