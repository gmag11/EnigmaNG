#ifndef MESH_WEBUI_H
#define MESH_WEBUI_H

#include <Arduino.h>
#include <esp_http_server.h>

// Forward declaration
class MeshNetwork;

class WebUI {
public:
    bool begin(uint16_t port = 80, const char* username = "admin",
               const char* password = "admin", MeshNetwork* mesh = nullptr);
    void stop();

    // Prometheus metrics endpoint
    bool startPrometheus(uint16_t port = 9090);

private:
    httpd_handle_t _server = nullptr;
    httpd_handle_t _prometheusServer = nullptr;
    MeshNetwork* _mesh = nullptr;
    char _username[32] = {};
    char _password[32] = {};
    char _realm[32] = "EnigmaNG";

    // HTTP Digest Auth
    bool _checkDigestAuth(httpd_req_t* req);
    static esp_err_t _handleRoot(httpd_req_t* req);
    static esp_err_t _handleStatus(httpd_req_t* req);
    static esp_err_t _handleNodes(httpd_req_t* req);
    static esp_err_t _handleRoutes(httpd_req_t* req);
    static esp_err_t _handlePeers(httpd_req_t* req);
    static esp_err_t _handleTopology(httpd_req_t* req);
    static esp_err_t _handleMetrics(httpd_req_t* req);
    static esp_err_t _handleConfig(httpd_req_t* req);
    static esp_err_t _handleConfigPost(httpd_req_t* req);
};

#endif // MESH_WEBUI_H
