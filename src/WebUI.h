#ifndef MESH_WEBUI_H
#define MESH_WEBUI_H

#if !defined(ESP8266)

#include <Arduino.h>
#include <esp_http_server.h>
#include "DnsProxy.h"

// Forward declaration
class MeshNetwork;

class WebUI {
public:
    bool begin(uint16_t port = 80, const char* username = "admin",
               const char* password = "admin", MeshNetwork* mesh = nullptr);
    void stop();

    // Attach DNS proxy for DNS management endpoints (call before begin()).
    void setDnsProxy(DnsProxy* proxy) { _dns = proxy; }

    // Prometheus metrics endpoint
    bool startPrometheus(uint16_t port = 9090);

private:
    httpd_handle_t _server = nullptr;
    httpd_handle_t _prometheusServer = nullptr;
    MeshNetwork* _mesh = nullptr;
    DnsProxy*    _dns  = nullptr;
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

    // DNS management endpoints
    static esp_err_t _handleDnsRecordsGet(httpd_req_t* req);    // GET  /api/v1/dns/records
    static esp_err_t _handleDnsRecordsPost(httpd_req_t* req);   // POST /api/v1/dns/records
    static esp_err_t _handleDnsRecordsDelete(httpd_req_t* req); // DELETE /api/v1/dns/records/*
    static esp_err_t _handleDnsCache(httpd_req_t* req);          // GET  /api/v1/dns/cache
    static esp_err_t _handleDnsPage(httpd_req_t* req);           // GET  /dns
};

#endif // !ESP8266
#endif // MESH_WEBUI_H
