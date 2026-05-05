#include "BatteryNode.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <cstring>

// RTC memory for surviving deep sleep
RTC_DATA_ATTR static BatteryState s_batteryState;

void BatteryNode::begin(uint32_t sleepIntervalSec) {
    _state = &s_batteryState;
    _state->sleepIntervalSec = sleepIntervalSec;
    _loadStateFromNVS();
}

void BatteryNode::runCycle() {
    _cycleStartMs = millis();

    // 1. TX UPLINK to current parent
    _txUplink();

    // 2. RX1 window: wait for downlink messages
    _rxWindow(BATTERY_RX1_WINDOW_MS);

    // 3. RX2 window: second chance for pending messages
    _rxWindow(BATTERY_RX2_WINDOW_MS);

    // 4. Enter deep sleep
    enterDeepSleep();
}

bool BatteryNode::addParentCandidate(const uint8_t* mac, int8_t rssi) {
    if (_state->parentCount >= BATTERY_MAX_PARENTS) {
        // Replace worst candidate (lowest RSSI already known)
        // For simplicity, replace the last one
        memcpy(_state->parentMacs[BATTERY_MAX_PARENTS - 1], mac, 6);
    } else {
        memcpy(_state->parentMacs[_state->parentCount], mac, 6);
        _state->parentCount++;
    }
    _saveStateToNVS();
    return true;
}

const uint8_t* BatteryNode::getCurrentParent() {
    if (_state->parentCount == 0) return nullptr;
    return _state->parentMacs[_state->currentParentIdx];
}

time_t BatteryNode::getMeshTime() {
    return _state->lastMeshTime;
}

void BatteryNode::setMeshTime(time_t t) {
    _state->lastMeshTime = t;
}

void BatteryNode::enterDeepSleep() {
    _saveStateToNVS();
    esp_sleep_enable_timer_wakeup((uint64_t)_state->sleepIntervalSec * 1000000ULL);
    esp_deep_sleep_start();
}

void BatteryNode::_txUplink() {
    // Send UPLINK frame to current parent
    // Actual frame construction delegated to LinkLayer + Crypto
    // TODO: Integrate with MeshNetwork frame sending
}

bool BatteryNode::_rxWindow(uint32_t durationMs) {
    uint32_t start = millis();
    while (millis() - start < durationMs) {
        // Poll for incoming frames
        // TODO: Integrate with PhysicalLayer receive
        delay(10);
    }
    return false;
}

void BatteryNode::_selectBestParent() {
    // Try next parent if current is unresponsive
    if (_state->parentCount > 1) {
        _state->currentParentIdx = (_state->currentParentIdx + 1) % _state->parentCount;
    }
}

void BatteryNode::_saveStateToNVS() {
    nvs_handle_t handle;
    if (nvs_open("mesh_bat", NVS_READWRITE, &handle) == ESP_OK) {
        nvs_set_blob(handle, "state", _state, sizeof(BatteryState));
        nvs_commit(handle);
        nvs_close(handle);
    }
}

void BatteryNode::_loadStateFromNVS() {
    nvs_handle_t handle;
    if (nvs_open("mesh_bat", NVS_READONLY, &handle) == ESP_OK) {
        size_t len = sizeof(BatteryState);
        nvs_get_blob(handle, "state", _state, &len);
        nvs_close(handle);
    }
}

// DownlinkManager implementation

DownlinkBuffer* DownlinkManager::_findBuffer(const uint8_t* childMac) {
    for (size_t i = 0; i < _childCount; i++) {
        if (memcmp(_buffers[i].childMac, childMac, 6) == 0) {
            return &_buffers[i];
        }
    }
    return nullptr;
}

DownlinkBuffer* DownlinkManager::_allocBuffer(const uint8_t* childMac) {
    if (_childCount >= MAX_CHILDREN) return nullptr;
    DownlinkBuffer* buf = &_buffers[_childCount++];
    memset(buf, 0, sizeof(DownlinkBuffer));
    memcpy(buf->childMac, childMac, 6);
    return buf;
}

bool DownlinkManager::bufferMessage(const uint8_t* childMac, const uint8_t* data, size_t len) {
    if (len > BATTERY_DOWNLINK_MSG_SIZE) return false;

    DownlinkBuffer* buf = _findBuffer(childMac);
    if (!buf) buf = _allocBuffer(childMac);
    if (!buf) return false;

    if (buf->count >= BATTERY_DOWNLINK_BUF_SIZE) {
        // FIFO: drop oldest
        buf->tail = (buf->tail + 1) % BATTERY_DOWNLINK_BUF_SIZE;
        buf->count--;
    }

    memcpy(buf->messages[buf->head], data, len);
    buf->msgLengths[buf->head] = len;
    buf->head = (buf->head + 1) % BATTERY_DOWNLINK_BUF_SIZE;
    buf->count++;
    return true;
}

size_t DownlinkManager::drainBuffer(const uint8_t* childMac, uint8_t* outBuf, size_t outBufLen) {
    DownlinkBuffer* buf = _findBuffer(childMac);
    if (!buf || buf->count == 0) return 0;

    size_t totalSent = 0;
    while (buf->count > 0 && totalSent + buf->msgLengths[buf->tail] <= outBufLen) {
        size_t msgLen = buf->msgLengths[buf->tail];
        memcpy(outBuf + totalSent, buf->messages[buf->tail], msgLen);
        totalSent += msgLen;
        buf->tail = (buf->tail + 1) % BATTERY_DOWNLINK_BUF_SIZE;
        buf->count--;
    }
    return totalSent;
}

bool DownlinkManager::hasMessages(const uint8_t* childMac) {
    DownlinkBuffer* buf = _findBuffer(childMac);
    return buf && buf->count > 0;
}
