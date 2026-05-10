# Spec: ESP8266 Support — `src8266/MeshNode8266`

**Reference:** §10 of EnigmaNG Specs v2.md

> **Structure:** ESP8266 code lives in the same `src/` directory of the repository, using preprocessor guards (`#ifdef ESP8266` / `#ifndef ESP8266`) to separate platform-specific code. This is the standard approach for multi-platform Arduino libraries and is compatible with PlatformIO registry and Arduino Library Manager. Shared protocol constants are in `src/Protocol.h` without guards.

## Purpose

Allow ESP8266 nodes to participate in the EnigmaNG network as MQTT publishers/subscribers via an ESP32 relay acting as a proxy.

## Accepted limitations

- **No relay:** ESP8266 cannot forward frames for other nodes.
- **No native IP stack:** It does not implement a virtual `netif` (would require patching Arduino Core ESP8266).
- **No bridge:** Cannot act as a gateway.
- **Proxy-only MQTT:** Covers ~95% of real IoT use cases.

## Architecture: MQTT proxy

```
[ESP8266] ──ESP-NOW──▶ [ESP32 Proxy] ──TCP/MQTT──▶ [MQTT Broker]
           PROXY_MSG                 MQTT publish/subscribe
```

The ESP32 proxy receives `PROXY_MSG` frames from the ESP8266 and converts them into MQTT messages toward the broker configured on the gateway.

## Proxy protocol

```cpp
enum ProxyMsgType : uint8_t {
    PROXY_CONNECT      = 0x01,
    PROXY_PUBLISH      = 0x02,
    PROXY_SUBSCRIBE    = 0x03,
    PROXY_UNSUBSCRIBE  = 0x04,
    PROXY_MESSAGE      = 0x05,  // Broker → ESP8266
    PROXY_PUBACK       = 0x06,  // QoS1 acknowledgement
    PROXY_DISCONNECT   = 0x07,
    PROXY_DISCOVERY    = 0x08,  // Broadcast: find available proxy
    PROXY_OFFER        = 0x09   // Response to discovery
};
```

**Proxy selection:**
1. ESP8266 sends `PROXY_DISCOVERY` broadcast.
2. Neighboring ESP32s reply with `PROXY_OFFER` including their MAC and RSSI.
3. ESP8266 picks the best RSSI and sends `PROXY_CONNECT`.

## Broker configuration

- Broker address: configured on the gateway, distributed in the provisioning response (§5.1) and in the `JOIN_BEACON`:
  ```json
  { "broker": "192.168.1.100:1883", "broker_user": "", "broker_pass": "" }
  ```
- ESP8266 stores the address in EEPROM after first provisioning.
- It updates if it receives a `JOIN_BEACON` with a different address.

## Public API for ESP8266

```cpp
class MeshNode8266 {
public:
    bool begin(const char* psk);
    bool mqttPublish(const char* topic, const uint8_t* payload,
                     size_t len, uint8_t qos = 0, bool retain = false);
    bool mqttSubscribe(const char* topic, uint8_t qos = 0);
    bool mqttUnsubscribe(const char* topic);
    void onMqttMessage(MqttCallback cb);
    void onConnected(ConnectCallback cb);
    void setSleepDuration(uint32_t seconds);
    bool isConnected();
    void loop();
};
```

## Delivery confirmation

- ESP-NOW provides peer-to-peer delivery confirmation at hardware level (`OnDataSent`). Sufficient for the direct hop ESP8266↔ESP32 proxy.
- For QoS 1/2: the broker provides PUBACK. The proxy forwards it to the ESP8266 via `PROXY_PUBACK`.

## Acceptance criteria

- Test: ESP8266 performs `PROXY_DISCOVERY`, selects an ESP32 proxy, publishes to a topic. Verify the message reaches the MQTT broker.
- Test: broker publishes to a topic subscribed by the ESP8266. Verify `onMqttMessage` is called on the ESP8266.
- Test: ESP8266 with incorrect PSK cannot connect (invalid JOIN_BEACON tag).
- Test: broker change on the gateway. ESP8266 receives new address in next JOIN_BEACON and applies it.
