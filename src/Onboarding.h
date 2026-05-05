#ifndef MESH_ONBOARDING_H
#define MESH_ONBOARDING_H

#include <Arduino.h>
#include <esp_http_server.h>

#define ONBOARDING_SSID_PREFIX     "ENIGMA-"
#define JOIN_BEACON_INTERVAL_MS    5000
#define CHANNEL_SCAN_TIME_MS       200
#define CHANNEL_SCAN_ORDER         {1, 6, 11, 2, 3, 4, 5, 7, 8, 9, 10, 12, 13}
#define CHANNEL_SCAN_ORDER_SIZE    13

class Onboarding {
public:
    // Gateway: start AP for provisioning
    bool startProvisioningAP(const uint8_t* networkId, uint8_t channel, const char* psk);
    void stopProvisioningAP();

    // Start provisioning HTTP server on AP interface (GET /provision)
    bool startProvisioningHTTP(uint16_t port = 80);
    void stopProvisioningHTTP();

    // Credentials of the running provisioning AP
    const char* getProvisioningSSID()     const { return _apSSID; }
    const char* getProvisioningPassword() const { return _apPassword; }

    // Gateway: send JOIN_BEACON periodically
    void sendJoinBeacon();

    // Node: blind channel scan
    bool startChannelScan();
    uint8_t getFoundChannel() { return _foundChannel; }
    bool isScanComplete() { return _scanComplete; }

    // Provisioning data
    struct ProvisioningData {
        uint8_t channel;
        uint8_t networkId[2];
        char psk[64];
        char mqttBroker[64];
        uint16_t mqttPort;
    };

    // Set the data returned by GET /provision
    void setProvisioningData(const ProvisioningData& data) { _provData = data; _provDataSet = true; }

    void update();

private:
    uint8_t _foundChannel = 0;
    bool _scanComplete = false;
    bool _apActive = false;
    uint32_t _lastBeaconMs = 0;
    uint8_t _scanIndex = 0;
    uint32_t _scanStartMs = 0;
    char _apSSID[32]     = {};
    char _apPassword[17] = {};

    // Provisioning HTTP server
    httpd_handle_t _provServer = nullptr;
    ProvisioningData _provData = {};
    bool _provDataSet = false;

    static esp_err_t _handleProvision(httpd_req_t* req);
};

#endif // MESH_ONBOARDING_H
