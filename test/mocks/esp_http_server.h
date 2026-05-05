#pragma once
// esp_http_server.h stub for native unit test builds.
#include "esp_wifi.h"   // for esp_err_t / ESP_OK
#include <stdint.h>
#include <string.h>

typedef void* httpd_handle_t;

typedef enum { HTTP_GET = 0, HTTP_POST, HTTP_PUT, HTTP_DELETE } httpd_method_t;

typedef struct httpd_req {
    const char* uri;
    httpd_method_t method;
    void* user_ctx;
} httpd_req_t;

typedef esp_err_t (*httpd_handler_t)(httpd_req_t*);

typedef struct {
    const char*     uri;
    httpd_method_t  method;
    httpd_handler_t handler;
    void*           user_ctx;
} httpd_uri_t;

typedef struct {
    uint16_t server_port;
    int      max_uri_handlers;
    bool     lru_purge_enable;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() httpd_config_t{80, 8}
#define HTTPD_401_UNAUTHORIZED  401
#define HTTPD_400_BAD_REQUEST   400
#define HTTPD_404_NOT_FOUND     404
#define HTTPD_500_INTERNAL_SERVER_ERROR 500

inline esp_err_t httpd_start(httpd_handle_t* h, const httpd_config_t*) { if (h) *h = (void*)1; return ESP_OK; }
inline esp_err_t httpd_stop(httpd_handle_t h)  { (void)h; return ESP_OK; }
inline esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*) { return ESP_OK; }

inline size_t httpd_req_get_hdr_value_len(httpd_req_t*, const char*) { return 0; }
inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*, const char*, char*, size_t) { return ESP_OK; }
inline esp_err_t httpd_req_get_url_query_str(httpd_req_t*, char*, size_t) { return ESP_OK; }
inline int  httpd_req_recv(httpd_req_t*, char*, size_t) { return 0; }
inline esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_set_type(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_send(httpd_req_t*, const char*, int) { return ESP_OK; }
inline esp_err_t httpd_resp_sendstr(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_send_chunk(httpd_req_t*, const char*, int) { return ESP_OK; }
inline esp_err_t httpd_resp_send_err(httpd_req_t*, int, const char*) { return ESP_OK; }
