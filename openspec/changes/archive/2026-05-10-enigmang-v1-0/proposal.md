# Proposal: EnigmaNG v1.0 — Complete Implementation

## What is being built?

EnigmaNG is an Arduino/IDF library for ESP32, compatible with PlatformIO, that creates a secure mesh network over ESP-NOW with complete IP transparency. Mesh nodes can use standard TCP/UDP (MQTT, HTTP, etc.) as if they were connected to WiFi, without modifying application libraries.

## Why?

EnigmaIOT (the author's previous library) does not have IP transparency: applications must use EnigmaIOT's proprietary API to send data. EnigmaNG eliminates that limitation by adding a virtual lwIP netif (`mesh0`) that makes the mesh completely transparent to standard TCP/IP applications.

## v1.0 Scope

### Includes

- **Physical layer:** Wrapper over QuickESPNow with RSSI EWMA and channel management.
- **Link layer:** 22B header + 12B tag frame. `Protocol` field (EtherType-like). 15 frame types.
- **Cryptography:** Curve25519 ECDH + HKDF-SHA256 + AES-128-GCM. Key rotation via Epoch + KEY_NACK. PeerManager with hash table.
- **Routing:** Proactive DVR (RIPv2-like). Seen-Frame Cache. Split Horizon + Poison Reverse. Up to ~100 nodes.
- **Transparent IP:** Virtual netif `mesh0` via esp_netif. MTU 216 bytes. TCP MSS 176 bytes. ARP, DHCP, ICMP.
- **Onboarding:** Permanent AP on gateway. HTTP provisioning. Blind channel search as fallback.
- **Battery nodes:** LoRaWAN Class A cycle. Downlink buffer in Parent. 3 Parent candidates in NVS.
- **Gateway:** Dual WiFi STA+AP. LAN routing + Internet NAT. Web UI (esp_http_server + HTTP Digest Auth). Prometheus `/metrics`.
- **ESP8266:** Derived library. MQTT Proxy only via ESP32 relay.
- **Service discovery:** Lightweight proprietary protocol + mDNS bridge on gateway.
- **Public API:** `MeshNetwork` compatible with PubSubClient, HTTPClient. `getClient()` returns `WiFiClient`.

### Not included (out of scope)

- Certificate-based authentication (postponed to v1.1 — §13).
- IPv6 (reserved for v1.x — Protocol field `0x02` reserved).
- ESPHome integration (reserved for v1.x — §16).
- Dual-board ESP-Hosted gateway (advanced option, documented in §9.5 but as separate IDF example, not in the main library).
- OTA over the mesh (postponed).
- More than ~100 nodes (practical ESP-NOW limit, not designed to exceed that in v1.0).

## Target platforms

| Platform | Support | Notes |
|-----------|---------|-------|
| Arduino Core ESP32 3.3.8 (IDF 5.5.4) | ✅ Full | Primary platform |
| ESP8266 Arduino Core | ✅ Partial | `MeshNode8266` only (MQTT Proxy) |
| Native IDF (CMake) | ✅ via idf_component/ | Needed for dual-board gateway |

## Phase estimation

12 development phases defined in §15 of the spec. See `tasks.md` for the complete breakdown.

## Main risks

| Risk | Mitigation |
|--------|-----------|
| QuickESPNow compatibility with IDF 5.5.4 | Verify in Phase 0 before building anything on top |
| `ip_napt` from lwIP not available in IDF 5.5.4 | Custom implementation via raw socket (already planned) |
| Insufficient heap on ESP32 with many peers | LRU eviction + `PEER_HEAP_LOW_WATERMARK` already designed |
| Complex L2 fragmentation | Used only for payloads &gt; 216B; TCP MSS avoids this for TCP |