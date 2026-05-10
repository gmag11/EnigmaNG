# Proposal: EnigmaNG v1.0 — Full Implementation

## What will be built?

EnigmaNG is an Arduino/IDF library for ESP32, compatible with PlatformIO, that builds a secure mesh over ESP-NOW with full IP transparency. Mesh nodes can use standard TCP/UDP (MQTT, HTTP, etc.) as if connected to WiFi, without modifying application libraries.

## Why?

The previous library (EnigmaIOT) lacked IP transparency: applications had to use a proprietary API to send data. EnigmaNG removes this limitation by adding a virtual lwIP netif (`mesh0`) that makes the mesh fully transparent to standard TCP/IP applications.

## v1.0 scope

### Included

- Physical layer: Wrapper over QuickESPNow with RSSI EWMA and channel management.
- Link layer: 22B header + 12B tag frame. `Protocol` field (EtherType-like). 15 frame types.
- Cryptography: Curve25519 ECDH + HKDF-SHA256 + AES-128-GCM. Key rotation via Epoch + KEY_NACK. PeerManager with hash table.
- Routing: Proactive DVR (RIPv2-like). Seen-Frame Cache. Split Horizon + Poison Reverse. Up to ~100 nodes.
- IP transparency: virtual netif `mesh0` via esp_netif. MTU 216 bytes. TCP MSS 176 bytes. ARP, DHCP, ICMP.
- Onboarding: Permanent AP on gateway. HTTP provisioning. Blind channel scan fallback.
- Battery nodes: LoRaWAN Class A-like cycle. Parent downlink buffer. 3 Parent candidates in NVS.
- Gateway: WiFi STA+AP dual. LAN routing + Internet NAT. Web UI (esp_http_server + HTTP Digest Auth). Prometheus `/metrics`.
- ESP8266: derived library. Proxy MQTT only via ESP32 relay.
- Service discovery: lightweight protocol + mDNS bridge on gateway.
- Public API: `MeshNetwork` compatible with PubSubClient, HTTPClient. `getClient()` returns `WiFiClient`.

### Excluded (out of scope)

- Certificate-based authentication (postponed to v1.1 — §13).
- IPv6 (reserved for v1.x — Protocol field `0x02` reserved).
- ESPHome integration (reserved for v1.x — §16).
- Dual-board ESP-Hosted gateway (advanced option, documented in §9.5 but as a separate IDF example, not main library).
- OTA over the mesh (postponed).
- >~100 nodes (practical ESP-NOW limit; not targeted in v1.0).

## Target platforms

| Platform | Support | Notes |
|----------|---------|-------|
| Arduino Core ESP32 3.3.8 (IDF 5.5.4) | ✅ Full | Primary platform |
| ESP8266 Arduino Core | ✅ Partial | Only `MeshNode8266` (MQTT Proxy) |
| Native IDF (CMake) | ✅ via idf_component/ | Required for dual-board gateway example |

## Phase estimate

12 development phases defined in §15 of the spec. See `tasks.md` for the full breakdown.

## Main risks

| Risk | Mitigation |
|------|------------|
| QuickESPNow compatibility with IDF 5.5.4 | Verify in Phase 0 before building on top |
| lwIP `ip_napt` missing in IDF 5.5.4 | Custom implementation via raw socket planned |
| Insufficient heap on ESP32 with many peers | LRU eviction + `PEER_HEAP_LOW_WATERMARK` design |
| Complex L2 fragmentation | Used only for payloads > 216B; TCP MSS prevents this for TCP |
