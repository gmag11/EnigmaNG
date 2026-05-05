#include "Onboarding.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include "mbedtls/md.h"
#include <cstring>

bool Onboarding::startProvisioningAP(const uint8_t* networkId, uint8_t channel, const char* psk) {
    // Build SSID: ENIGMA-<NetworkID hex>-CH<channel>
    char ssid[32];
    snprintf(ssid, sizeof(ssid), ONBOARDING_SSID_PREFIX "%02X%02X-CH%d",
             networkId[0], networkId[1], channel);

    // Password: HMAC(PSK, "onboarding")[:8] as hex
    uint8_t hmacOut[32];
    const mbedtls_md_info_t* md = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(md, (const uint8_t*)psk, strlen(psk),
                    (const uint8_t*)"onboarding", 10, hmacOut);

    char password[17];
    for (int i = 0; i < 8; i++) {
        snprintf(&password[i * 2], 3, "%02x", hmacOut[i]);
    }
    password[16] = '\0';

    // Start SoftAP
    WiFi.softAP(ssid, password, channel);
    _apActive = true;

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
