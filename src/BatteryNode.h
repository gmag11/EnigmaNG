#ifndef MESH_BATTERY_NODE_H
#define MESH_BATTERY_NODE_H

#if !defined(ESP8266)

#include <Arduino.h>
#include <esp_sleep.h>

#define BATTERY_MAX_PARENTS        3
#define BATTERY_RX1_WINDOW_MS      2000
#define BATTERY_RX2_WINDOW_MS      2000
#define BATTERY_DOWNLINK_BUF_SIZE  5
#define BATTERY_DOWNLINK_MSG_SIZE  200

// Stored in RTC memory to survive deep sleep
struct BatteryState {
    uint8_t  parentMacs[BATTERY_MAX_PARENTS][6];
    uint8_t  parentCount;
    uint8_t  currentParentIdx;
    uint8_t  lastEpoch;
    uint32_t sleepIntervalSec;
    time_t   lastMeshTime;
};

// Downlink buffer (stored in Parent's RAM for each battery child)
struct DownlinkBuffer {
    uint8_t  childMac[6];
    uint8_t  messages[BATTERY_DOWNLINK_BUF_SIZE][BATTERY_DOWNLINK_MSG_SIZE];
    size_t   msgLengths[BATTERY_DOWNLINK_BUF_SIZE];
    uint8_t  head;
    uint8_t  tail;
    uint8_t  count;
};

class BatteryNode {
public:
    // Configure battery mode
    void begin(uint32_t sleepIntervalSec);

    // Main cycle: WAKE → TX UPLINK → RX1 → RX2 → DEEP SLEEP
    void runCycle();

    // Parent management (stored in NVS)
    bool addParentCandidate(const uint8_t* mac, int8_t rssi);
    const uint8_t* getCurrentParent();

    // Time sync
    time_t getMeshTime();
    void setMeshTime(time_t t);

    // Deep sleep
    void enterDeepSleep();

private:
    BatteryState* _state = nullptr;
    uint32_t _cycleStartMs = 0;

    void _txUplink();
    bool _rxWindow(uint32_t durationMs);
    void _selectBestParent();
    void _saveStateToNVS();
    void _loadStateFromNVS();
};

// Parent-side downlink buffer management
class DownlinkManager {
public:
    bool bufferMessage(const uint8_t* childMac, const uint8_t* data, size_t len);
    size_t drainBuffer(const uint8_t* childMac, uint8_t* outBuf, size_t outBufLen);
    bool hasMessages(const uint8_t* childMac);

private:
    static constexpr size_t MAX_CHILDREN = 5;
    DownlinkBuffer _buffers[MAX_CHILDREN];
    size_t _childCount = 0;

    DownlinkBuffer* _findBuffer(const uint8_t* childMac);
    DownlinkBuffer* _allocBuffer(const uint8_t* childMac);
};

#endif // !ESP8266
#endif // MESH_BATTERY_NODE_H
