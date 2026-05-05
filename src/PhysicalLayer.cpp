#include "PhysicalLayer.h"
#include <cstring>

MeshPhysicalLayer* MeshPhysicalLayer::_instance = nullptr;

bool MeshPhysicalLayer::begin(uint8_t channel, const uint8_t* networkId) {
    _instance = this;
    _channel = channel;
    memcpy(_networkId, networkId, 2);

    quickEspNow.onDataRcvd(_onDataRecv);

    if (!quickEspNow.begin(channel)) {
        return false;
    }
    return true;
}

bool MeshPhysicalLayer::sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len) {
    if (len > 250) return false;
    return quickEspNow.send(dstMac, data, len) == 0;
}

bool MeshPhysicalLayer::sendBroadcast(const uint8_t* data, size_t len) {
    if (len > 250) return false;
    static const uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    return quickEspNow.send(broadcast, data, len) == 0;
}

void MeshPhysicalLayer::onReceive(MeshRecvCallback cb) {
    _recvCb = cb;
}

int8_t MeshPhysicalLayer::getLastRssi() {
    return _lastRssi;
}

void MeshPhysicalLayer::setChannel(uint8_t channel) {
    _channel = channel;
    quickEspNow.stop();
    quickEspNow.begin(channel);
}

bool MeshPhysicalLayer::setTxPower(int8_t power) {
    // ESP-NOW uses WiFi TX power
    esp_wifi_set_max_tx_power(power * 4); // IDF uses 0.25 dBm units
    return true;
}

float MeshPhysicalLayer::getRssiEwma(const uint8_t* mac) {
    int idx = _findRssiEntry(mac);
    if (idx < 0) return 0.0f;
    return _rssiTable[idx].ewma;
}

void MeshPhysicalLayer::setRssiAlpha(float alpha) {
    _rssiAlpha = alpha;
}

void MeshPhysicalLayer::setPeerInactivityTimeout(uint32_t ms) {
    _peerInactivityTimeout = ms;
}

void MeshPhysicalLayer::update() {
    uint32_t now = millis();
    for (size_t i = 0; i < MAX_RSSI_ENTRIES; i++) {
        if (_rssiTable[i].valid && (now - _rssiTable[i].lastSeen > _peerInactivityTimeout)) {
            _rssiTable[i].valid = false;
            _rssiTable[i].ewma = 0.0f;
        }
    }
}

void MeshPhysicalLayer::_onDataRecv(const uint8_t* mac, const uint8_t* data, int len, signed int rssi, bool broadcast) {
    if (!_instance) return;

    _instance->_lastRssi = (int8_t)rssi;
    _instance->_updateRssi(mac, (int8_t)rssi);

    if (_instance->_recvCb) {
        _instance->_recvCb(mac, data, (size_t)len, (int8_t)rssi);
    }
}

void MeshPhysicalLayer::_updateRssi(const uint8_t* mac, int8_t rssi) {
    int idx = _findRssiEntry(mac);

    if (idx < 0) {
        // Find empty slot or oldest entry
        int emptySlot = -1;
        uint32_t oldest = UINT32_MAX;
        int oldestIdx = 0;

        for (size_t i = 0; i < MAX_RSSI_ENTRIES; i++) {
            if (!_rssiTable[i].valid) {
                emptySlot = i;
                break;
            }
            if (_rssiTable[i].lastSeen < oldest) {
                oldest = _rssiTable[i].lastSeen;
                oldestIdx = i;
            }
        }

        idx = (emptySlot >= 0) ? emptySlot : oldestIdx;
        memcpy(_rssiTable[idx].mac, mac, 6);
        _rssiTable[idx].ewma = (float)rssi;
        _rssiTable[idx].valid = true;
    } else {
        // EWMA: rssi_avg = α × rssi_new + (1-α) × rssi_avg
        _rssiTable[idx].ewma = _rssiAlpha * (float)rssi + (1.0f - _rssiAlpha) * _rssiTable[idx].ewma;
    }

    _rssiTable[idx].lastSeen = millis();
}

int MeshPhysicalLayer::_findRssiEntry(const uint8_t* mac) {
    for (size_t i = 0; i < MAX_RSSI_ENTRIES; i++) {
        if (_rssiTable[i].valid && memcmp(_rssiTable[i].mac, mac, 6) == 0) {
            return (int)i;
        }
    }
    return -1;
}
