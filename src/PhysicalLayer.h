#ifndef MESH_PHYSICAL_LAYER_H
#define MESH_PHYSICAL_LAYER_H

#include <Arduino.h>
#include <QuickEspNow.h>

// Callback type: (srcMac, data, len, rssi)
typedef void (*MeshRecvCallback)(const uint8_t* srcMac, const uint8_t* data, size_t len, int8_t rssi);

// RSSI defaults
#define RSSI_CONNECT_THRESHOLD_DEFAULT    (-75)
#define RSSI_DISCONNECT_THRESHOLD_DEFAULT (-85)
#define RSSI_EWMA_ALPHA_DEFAULT           (0.3f)
#define PEER_INACTIVITY_TIMEOUT_DEFAULT   (120000UL) // 120s in ms

class MeshPhysicalLayer {
public:
    bool begin(uint8_t channel, const uint8_t* networkId);
    bool sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len);
    bool sendBroadcast(const uint8_t* data, size_t len);
    void onReceive(MeshRecvCallback cb);
    int8_t getLastRssi();
    void setChannel(uint8_t channel);
    bool setTxPower(int8_t power);

    // RSSI management
    float getRssiEwma(const uint8_t* mac);
    void setRssiAlpha(float alpha);
    void setPeerInactivityTimeout(uint32_t ms);

    void update(); // Call periodically to check timeouts

private:
    uint8_t _channel = 1;
    uint8_t _networkId[2] = {0};
    int8_t _lastRssi = 0;
    float _rssiAlpha = RSSI_EWMA_ALPHA_DEFAULT;
    uint32_t _peerInactivityTimeout = PEER_INACTIVITY_TIMEOUT_DEFAULT;
    MeshRecvCallback _recvCb = nullptr;

    static void _onDataRecv(const uint8_t* mac, const uint8_t* data, int len, signed int rssi, bool broadcast);
    static MeshPhysicalLayer* _instance;

    // RSSI tracking per peer (simple array for now, moved to PeerManager later)
    struct RssiEntry {
        uint8_t mac[6];
        float ewma;
        uint32_t lastSeen;
        bool valid;
    };
    static constexpr size_t MAX_RSSI_ENTRIES = 32;
    RssiEntry _rssiTable[MAX_RSSI_ENTRIES] = {};
    void _updateRssi(const uint8_t* mac, int8_t rssi);
    int _findRssiEntry(const uint8_t* mac);
};

#endif // MESH_PHYSICAL_LAYER_H
