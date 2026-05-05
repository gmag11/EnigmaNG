#include "Onboarding.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "mbedtls/md.h"
#include <cstring>
#include <cstdio>

bool Onboarding::startProvisioningAP(const uint8_t* networkId, uint8_t channel, const char* psk) {
    // Build SSID: ENIGMA-<NetworkID hex>-CH<channel>
    snprintf(_apSSID, sizeof(_apSSID), ONBOARDING_SSID_PREFIX "%02X%02X-CH%d",
             networkId[0], networkId[1], channel);

    // Password: HMAC(PSK, "onboarding")[:8] as hex
    uint8_t hmacOut[32];
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(md, (const uint8_t*)psk, strlen(psk),
                    (const uint8_t*)"onboarding", 10, hmacOut);

    for (int i = 0; i < 8; i++) {
        snprintf(&_apPassword[i * 2], 3, "%02x", hmacOut[i]);
    }
    _apPassword[16] = '\0';

    // Fix AP IP explicitly — required in WIFI_AP_STA mode on Arduino ESP32 3.x
    bool cfgOk = WiFi.softAPConfig(
        IPAddress(192, 168, 4, 1),
        IPAddress(192, 168, 4, 1),
        IPAddress(255, 255, 255, 0)
    );
    Serial.printf("[Onboarding] softAPConfig = %s\n", cfgOk ? "OK" : "FAIL");

    // Start SoftAP
    Serial.printf("[Onboarding] softAP(%s, ****, ch%d)...\n", _apSSID, channel);
    bool apOk = WiFi.softAP(_apSSID, _apPassword, channel);
    Serial.printf("[Onboarding] softAP = %s | AP IP: %s\n",
                  apOk ? "OK" : "FAIL",
                  WiFi.softAPIP().toString().c_str());
    _apActive = apOk;

    return true;
}

void Onboarding::stopProvisioningAP() {
    if (_apActive) {
        WiFi.softAPdisconnect(true);
        _apActive = false;
    }
}

void Onboarding::sendJoinBeacon() {
    // JOIN_BEACON is sent via the mesh physical layer as a broadcast frame
    // This is called by the main loop; actual transmission delegated to MeshNetwork
    _lastBeaconMs = millis();
}

bool Onboarding::startChannelScan() {
    _scanComplete = false;
    _scanIndex = 0;
    _foundChannel = 0;
    _scanStartMs = millis();
    return true;
}

void Onboarding::update() {
    if (!_scanComplete && _scanIndex > 0) {
        // Channel scan in progress
        static const uint8_t scanOrder[CHANNEL_SCAN_ORDER_SIZE] = CHANNEL_SCAN_ORDER;

        if (millis() - _scanStartMs >= CHANNEL_SCAN_TIME_MS) {
            // Move to next channel
            _scanIndex++;
            if (_scanIndex >= CHANNEL_SCAN_ORDER_SIZE) {
                _scanComplete = true;
                return;
            }
            esp_wifi_set_channel(scanOrder[_scanIndex], WIFI_SECOND_CHAN_NONE);
            _scanStartMs = millis();
        }
    }

    // JOIN_BEACON sending (gateway only)
    if (_apActive && (millis() - _lastBeaconMs >= JOIN_BEACON_INTERVAL_MS)) {
        sendJoinBeacon();
    }
}

// ─── Provisioning HTTP Server ─────────────────────────────────────────────────

bool Onboarding::startProvisioningHTTP(uint16_t port) {
    if (_provServer) return true;  // already running

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = port;
    config.lru_purge_enable = true;

    if (httpd_start(&_provServer, &config) != ESP_OK) {
        Serial.println("[Onboarding] Failed to start provisioning HTTP server");
        return false;
    }

    httpd_uri_t provUri = {
        .uri      = "/provision",
        .method   = HTTP_GET,
        .handler  = _handleProvision,
        .user_ctx = this
    };
    httpd_register_uri_handler(_provServer, &provUri);

    Serial.printf("[Onboarding] Provisioning HTTP server on port %u\n", port);
    return true;
}

void Onboarding::stopProvisioningHTTP() {
    if (_provServer) {
        httpd_stop(_provServer);
        _provServer = nullptr;
    }
}

esp_err_t Onboarding::_handleProvision(httpd_req_t* req) {
    Onboarding* self = (Onboarding*)req->user_ctx;

    if (!self->_provDataSet) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Provisioning data not configured");
        return ESP_OK;
    }

    // Build JSON response with network credentials
    char json[256];
    int len = snprintf(json, sizeof(json),
        "{\"channel\":%u,\"networkId\":\"%02X%02X\",\"psk\":\"%s\","
        "\"mqttBroker\":\"%s\",\"mqttPort\":%u}",
        self->_provData.channel,
        self->_provData.networkId[0], self->_provData.networkId[1],
        self->_provData.psk,
        self->_provData.mqttBroker,
        self->_provData.mqttPort);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, len);
    return ESP_OK;
}
