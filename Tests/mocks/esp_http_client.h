#pragma once
// Stub esp_http_client.h for unit tests.
// Provides only the API surface that RegistrationServiceImpl + HttpUploadServiceImpl
// reference. Tests inject canned responses via http_test_set_response().

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    HTTP_EVENT_ERROR = 0,
    HTTP_EVENT_ON_CONNECTED,
    HTTP_EVENT_HEADERS_SENT,
    HTTP_EVENT_ON_HEADER,
    HTTP_EVENT_ON_DATA,
    HTTP_EVENT_ON_FINISH,
    HTTP_EVENT_DISCONNECTED,
    HTTP_EVENT_REDIRECT
} esp_http_client_event_id_t;

typedef enum {
    HTTP_METHOD_GET = 0,
    HTTP_METHOD_POST,
    HTTP_METHOD_PUT,
    HTTP_METHOD_PATCH,
    HTTP_METHOD_DELETE,
    HTTP_METHOD_HEAD
} esp_http_client_method_t;

typedef enum {
    HTTP_TRANSPORT_UNKNOWN = 0,
    HTTP_TRANSPORT_OVER_TCP,
    HTTP_TRANSPORT_OVER_SSL
} esp_http_client_transport_t;

struct esp_http_client;
typedef struct esp_http_client* esp_http_client_handle_t;

typedef struct {
    esp_http_client_event_id_t event_id;
    esp_http_client_handle_t client;
    void* data;
    int data_len;
    void* user_data;
    char* header_key;
    char* header_value;
} esp_http_client_event_t;

typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t* evt);

typedef struct {
    const char* url;
    const char* host;
    int port;
    const char* username;
    const char* password;
    esp_http_client_method_t method;
    int timeout_ms;
    http_event_handle_cb event_handler;
    esp_http_client_transport_t transport_type;
    void* user_data;
    int buffer_size;
    int buffer_size_tx;
    void* cert_pem;
    void* client_cert_pem;
    void* client_key_pem;
    bool use_global_ca_store;
    esp_err_t (*crt_bundle_attach)(void*);
    bool skip_cert_common_name_check;
} esp_http_client_config_t;

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t* config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client, const char* key, const char* value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client, const char* data, int len);
esp_err_t esp_http_client_set_method(esp_http_client_handle_t client, esp_http_client_method_t method);
esp_err_t esp_http_client_set_url(esp_http_client_handle_t client, const char* url);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
int esp_http_client_get_content_length(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_open(esp_http_client_handle_t client, int write_len);
int esp_http_client_write(esp_http_client_handle_t client, const char* buffer, int len);
int esp_http_client_fetch_headers(esp_http_client_handle_t client);
int esp_http_client_read(esp_http_client_handle_t client, char* buffer, int len);
esp_err_t esp_http_client_close(esp_http_client_handle_t client);

// Test injection helpers
void http_test_reset(void);
void http_test_set_response(const uint8_t* data, int len, int status_code);
void http_test_set_perform_result(esp_err_t result);
void http_test_set_open_result(esp_err_t result);
void http_test_set_write_fail_after(int bytes);
int http_test_get_last_post_len(void);
const uint8_t* http_test_get_last_post_data(void);

#ifdef __cplusplus
}
#endif
