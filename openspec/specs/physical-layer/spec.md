# Spec: Physical Layer

**Reference:** §3 of EnigmaNG Specs v2.md

## Purpose

Clean abstraction over QuickESPNow/ESP-NOW used by the rest of the library as the transport layer. Provides unicast/broadcast send, receive callback, RSSI per frame and channel management.

## Required interface

```cpp
class MeshPhysicalLayer {
public:
    bool begin(uint8_t channel, const uint8_t* networkId);
    bool sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len);
    bool sendBroadcast(const uint8_t* data, size_t len);
    void onReceive(MeshRecvCallback cb);
    int8_t getLastRssi();
    void setChannel(uint8_t channel);
    bool setTxPower(int8_t power);
};
```

## Channel management

- Single channel for the whole mesh (1–14).
- In single-chip gateway mode: channel forced by the connected WiFi AP (hardware limitation).
- Channel change: announced with `CONTROL/CHANNEL_CHANGE` + migration timestamp (+30s). Nodes keep their Link Keys.
- Nodes that lose contact after the change start a blind channel scan (§5.2).

## RSSI and range thresholds

| Parameter | Default value | Configurable |
|-----------|----------------|--------------|
| `RSSI_CONNECT_THRESHOLD` | -75 dBm | Yes |
| `RSSI_DISCONNECT_THRESHOLD` | -85 dBm | Yes |
| Hysteresis | 10 dB | No (derived) |
| EWMA α | 0.3 | Yes |
| `PEER_INACTIVITY_TIMEOUT` | 120s | Yes |

### EWMA formula

```
rssi_avg = 0.3 × new_rssi + 0.7 × rssi_avg
```

If no frames are received within `PEER_INACTIVITY_TIMEOUT`, `rssi_avg` is invalidated.

## Dependencies

- QuickESPNow (third-party library by the author).
- ESP-NOW API (IDF via Arduino Core ESP32 3.3.8).

## Acceptance criteria

- Test: 2 ESP32 on channel 6. Send 100 unicast frames. Verify delivery ≥ 95%, RSSI EWMA updated after each frame.
- Test: announced channel change. Both nodes migrate and resume communication without renegotiating the key.
