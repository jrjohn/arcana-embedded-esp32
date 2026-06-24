// Stub implementation of esp_http_client API for unit tests.
// Captures the event handler from init() and replays a canned response on
// perform() so test code can drive the response parsing path of code under
// test (e.g. RegistrationServiceImpl::httpRegister → parseResponse).

#include "esp_http_client.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
struct FakeClient {
    http_event_handle_cb handler;
    void* user_data;
    int status;
    std::vector<uint8_t> postData;
    size_t readOffset = 0;   // for streaming-mode read()
    bool openFails = false;  // simulate connect failure
};

static std::vector<uint8_t> g_responseData;
static int g_responseStatus = 200;
static esp_err_t g_performResult = ESP_OK;
static esp_err_t g_openResult = ESP_OK;
static std::vector<uint8_t> g_lastPost;
static int g_writeFailAfter = -1;  // -1 = never; otherwise fail after N bytes
static int g_writeBytesSoFar = 0;
static int g_writeStalls = 0;      // next N writes return 0 (TLS WANT_WRITE stall), then resume
} // namespace

extern "C" {

void http_test_reset(void) {
    g_responseData.clear();
    g_responseStatus = 200;
    g_performResult = ESP_OK;
    g_openResult = ESP_OK;
    g_lastPost.clear();
    g_writeFailAfter = -1;
    g_writeBytesSoFar = 0;
    g_writeStalls = 0;
}

void http_test_set_open_result(esp_err_t result) {
    g_openResult = result;
}

void http_test_set_write_fail_after(int bytes) {
    g_writeFailAfter = bytes;
    g_writeBytesSoFar = 0;
}

// Simulate `count` consecutive TLS WANT_WRITE stalls (esp_http_client_write
// returns 0) before writes resume — exercises the stall/retry path in uploadFile.
void http_test_set_write_stalls(int count) {
    g_writeStalls = count;
}

void http_test_set_response(const uint8_t* data, int len, int status_code) {
    g_responseData.assign(data, data + len);
    g_responseStatus = status_code;
}

void http_test_set_perform_result(esp_err_t result) {
    g_performResult = result;
}

int http_test_get_last_post_len(void) {
    return static_cast<int>(g_lastPost.size());
}

const uint8_t* http_test_get_last_post_data(void) {
    return g_lastPost.empty() ? nullptr : g_lastPost.data();
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config) {
    auto* c = new FakeClient;
    c->handler = config ? config->event_handler : nullptr;
    c->user_data = config ? config->user_data : nullptr;
    c->status = g_responseStatus;
    return reinterpret_cast<esp_http_client_handle_t>(c);
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    auto* c = reinterpret_cast<FakeClient*>(client);
    if (!c) return ESP_FAIL;
    if (g_performResult != ESP_OK) return g_performResult;

    // Replay canned response by invoking the event handler with HTTP_EVENT_ON_DATA
    if (c->handler && !g_responseData.empty()) {
        esp_http_client_event_t evt = {};
        evt.event_id = HTTP_EVENT_ON_DATA;
        evt.client = client;
        evt.data = g_responseData.data();
        evt.data_len = static_cast<int>(g_responseData.size());
        evt.user_data = c->user_data;
        c->handler(&evt);
    }
    c->status = g_responseStatus;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) {
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char* data, int len) {
    auto* c = reinterpret_cast<FakeClient*>(client);
    if (c && data && len > 0) {
        c->postData.assign(reinterpret_cast<const uint8_t*>(data),
                           reinterpret_cast<const uint8_t*>(data) + len);
        g_lastPost = c->postData;
    }
    return ESP_OK;
}

esp_err_t esp_http_client_set_method(esp_http_client_handle_t, esp_http_client_method_t) {
    return ESP_OK;
}

esp_err_t esp_http_client_set_url(esp_http_client_handle_t, const char*) {
    return ESP_OK;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    auto* c = reinterpret_cast<FakeClient*>(client);
    return c ? c->status : 0;
}

int esp_http_client_get_content_length(esp_http_client_handle_t) {
    return static_cast<int>(g_responseData.size());
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    auto* c = reinterpret_cast<FakeClient*>(client);
    delete c;
    return ESP_OK;
}

esp_err_t esp_http_client_open(esp_http_client_handle_t client, int) {
    auto* c = reinterpret_cast<FakeClient*>(client);
    if (c) c->readOffset = 0;
    return g_openResult;
}
int esp_http_client_write(esp_http_client_handle_t, const char* data, int len) {
    if (g_writeStalls > 0) {
        g_writeStalls--;
        return 0;   // TLS WANT_WRITE stall: socket buffer full, "retry" not fatal
    }
    if (g_writeFailAfter >= 0 && g_writeBytesSoFar + len > g_writeFailAfter) {
        return -1;  // simulate mid-stream failure
    }
    g_writeBytesSoFar += len;
    (void)data;
    return len;
}
int esp_http_client_fetch_headers(esp_http_client_handle_t) {
    return static_cast<int>(g_responseData.size());
}
int esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len) {
    auto* c = reinterpret_cast<FakeClient*>(client);
    if (!c) return -1;
    if (c->readOffset >= g_responseData.size()) return 0;
    int avail = static_cast<int>(g_responseData.size() - c->readOffset);
    int n = (len < avail) ? len : avail;
    memcpy(buffer, g_responseData.data() + c->readOffset, n);
    c->readOffset += n;
    return n;
}
esp_err_t esp_http_client_close(esp_http_client_handle_t) { return ESP_OK; }

} // extern "C"
