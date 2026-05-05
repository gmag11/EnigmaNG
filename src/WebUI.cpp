#include "WebUI.h"
#include "mbedtls/md5.h"
#include <cstring>
#include <cstdio>

bool WebUI::begin(uint16_t port, const char* username, const char* password) {
    strncpy(_username, username, sizeof(_username) - 1);
    strncpy(_password, password, sizeof(_password) - 1);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;

    if (httpd_start(&_server, &config) != ESP_OK) {
        return false;
    }

    // Register URI handlers
    httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = _handleRoot, .user_ctx = this };
    httpd_uri_t status = { .uri = "/api/v1/status", .method = HTTP_GET, .handler = _handleStatus, .user_ctx = this };
    httpd_uri_t nodes = { .uri = "/api/v1/nodes", .method = HTTP_GET, .handler = _handleNodes, .user_ctx = this };
    httpd_uri_t routes = { .uri = "/api/v1/routes", .method = HTTP_GET, .handler = _handleRoutes, .user_ctx = this };
    httpd_uri_t peers = { .uri = "/api/v1/peers", .method = HTTP_GET, .handler = _handlePeers, .user_ctx = this };

    httpd_register_uri_handler(_server, &root);
    httpd_register_uri_handler(_server, &status);
    httpd_register_uri_handler(_server, &nodes);
    httpd_register_uri_handler(_server, &routes);
    httpd_register_uri_handler(_server, &peers);

    return true;
}

void WebUI::stop() {
    if (_server) {
        httpd_stop(_server);
        _server = nullptr;
    }
    if (_prometheusServer) {
        httpd_stop(_prometheusServer);
        _prometheusServer = nullptr;
    }
}

bool WebUI::startPrometheus(uint16_t port) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;

    if (httpd_start(&_prometheusServer, &config) != ESP_OK) {
        return false;
    }

    httpd_uri_t metrics = { .uri = "/metrics", .method = HTTP_GET, .handler = _handleMetrics, .user_ctx = this };
    httpd_register_uri_handler(_prometheusServer, &metrics);

    return true;
}

bool WebUI::_checkDigestAuth(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;

    // Get Authorization header
    size_t authLen = httpd_req_get_hdr_value_len(req, "Authorization");
    if (authLen == 0) {
        // Send 401 with WWW-Authenticate header
        char authHeader[128];
        snprintf(authHeader, sizeof(authHeader),
                 "Digest realm=\"%s\", nonce=\"%08lx\", qop=\"auth\"",
                 self->_realm, (unsigned long)millis());
        httpd_resp_set_hdr(req, "WWW-Authenticate", authHeader);
        httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Authentication required");
        return false;
    }

    // Parse and validate digest — simplified for now
    // Full implementation would parse nonce, nc, cnonce, response and validate
    // TODO: Complete HTTP Digest Auth validation
    return true;
}

esp_err_t WebUI::_handleRoot(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    const char* html =
        "<!DOCTYPE html><html><head><title>EnigmaNG</title></head>"
        "<body><h1>EnigmaNG Mesh Dashboard</h1>"
        "<p><a href='/api/v1/status'>Status</a></p>"
        "<p><a href='/api/v1/nodes'>Nodes</a></p>"
        "<p><a href='/api/v1/routes'>Routes</a></p>"
        "<p><a href='/api/v1/peers'>Peers</a></p>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html, strlen(html));
    return ESP_OK;
}

esp_err_t WebUI::_handleStatus(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    // TODO: Populate with real data from MeshNetwork
    const char* json = "{\"uptime\":0,\"nodes\":0,\"routes\":0,\"freeHeap\":0}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebUI::_handleNodes(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    const char* json = "[]";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebUI::_handleRoutes(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    const char* json = "[]";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebUI::_handlePeers(httpd_req_t* req) {
    WebUI* self = (WebUI*)req->user_ctx;
    if (!self->_checkDigestAuth(req)) return ESP_OK;

    const char* json = "[]";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

esp_err_t WebUI::_handleMetrics(httpd_req_t* req) {
    // Prometheus format - no auth required on metrics port
    char metrics[512];
    snprintf(metrics, sizeof(metrics),
             "# HELP enigmang_nodes_total Total mesh nodes\n"
             "# TYPE enigmang_nodes_total gauge\n"
             "enigmang_nodes_total 0\n"
             "# HELP enigmang_routes_total Total routes in table\n"
             "# TYPE enigmang_routes_total gauge\n"
             "enigmang_routes_total 0\n"
             "# HELP enigmang_free_heap_bytes Free heap memory\n"
             "# TYPE enigmang_free_heap_bytes gauge\n"
             "enigmang_free_heap_bytes %lu\n"
             "# HELP enigmang_uptime_seconds Uptime in seconds\n"
             "# TYPE enigmang_uptime_seconds counter\n"
             "enigmang_uptime_seconds %lu\n",
             (unsigned long)ESP.getFreeHeap(),
             (unsigned long)(millis() / 1000));

    httpd_resp_set_type(req, "text/plain");
    httpd_resp_send(req, metrics, strlen(metrics));
    return ESP_OK;
}
