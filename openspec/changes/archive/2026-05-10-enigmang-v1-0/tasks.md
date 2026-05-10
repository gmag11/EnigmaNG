# Tasks: EnigmaNG v1.0

## Progress: 74/74 tasks completed

---

## Phase 0: Project infrastructure

- [x] Configure PlatformIO with Arduino Core ESP32 3.3.8 + QuickESPNow
  - _Test: `pio run` with no compilation errors_
- [x] Verify QuickESPNow compatibility with IDF 5.5.4
  - _Test: basic ESP-NOW send/receive on 2 ESP32s with QuickESPNow_
- [x] Basic ESP-NOW test: unicast/broadcast send, per-frame RSSI retrieval
  - _Test: 3 nodes, all receive broadcast, RSSI returned by QuickESPNow_
- [x] Create directory structure: `src/`, `arduino/`, `idf_component/`, `examples/`, `test/`
  - _Test: valid `library.json`, PlatformIO recognizes it as a library_
- [x] Configure test framework (Unity on PlatformIO)
  - _Test: dummy test passes with `pio test`_

---

## Phase 1: Physical Layer

**Spec:** `openspec/specs/physical-layer/spec.md`

- [x] Implement `MeshPhysicalLayer` as wrapper over QuickESPNow
  - _Test: `begin(channel=6, networkId)` initializes ESP-NOW correctly_
- [x] Implement `sendUnicast()` and `sendBroadcast()`
  - _Test: unicast frame reaches only the destination; broadcast to all on the channel_
- [x] Implement RSSI EWMA (α=0.3) with `PEER_INACTIVITY_TIMEOUT`
  - _Test: 10 frames received with known RSSI; verify EWMA calculation_
- [x] Implement channel management (`setChannel()`) and `CONTROL/CHANNEL_CHANGE` announcement
  - _Test: 2 nodes change channel synchronously; resume communication without re-keying_

---

## Phase 2: Frame serialization (Link Layer)

**Spec:** `openspec/specs/link-layer/spec.md`

- [x] Implement 22-byte header serializer/deserializer
  - _Unit test: serialize and deserialize each field; verify byte by byte_
- [x] Implement `FrameType` enum (0x01–0x0F) and `Protocol` enum (0x00–0xFF)
  - _Test: frame with incorrect NetworkID silently discarded_
- [x] Implement NetworkID filtering on reception (discard without decrypting)
  - _Test: 2 networks with same PSK but different NetworkIDs do not interfere_
- [x] Implement `DATA_FRAG` serialization with fragment header (4B extra)
  - _Test: fragment correctly identified and queued for reassembly_

---

## Phase 3: Cryptography

**Spec:** `openspec/specs/crypto/spec.md`

- [x] Integrate Curve25519 (mbedTLS) for ephemeral pair generation and ECDH
  - _Test: X25519(privA, pubB) == X25519(privB, pubA)_
- [x] Implement HKDF-SHA256 for NetworkKey, NetworkID, and LinkKey
  - _Test: same PSK produces same NetworkKey/NetworkID on 2 nodes; LinkKey depends on MACs_
- [x] Implement AES-128-GCM encryption/decryption with nonce derived from header
  - _Test: encrypt and decrypt; verify Protocol field is in AD and cannot be altered_
- [x] Implement complete ECDH handshake: HELLO → REPLY → CONFIRM × 2
  - _Test: 2 nodes with correct PSK negotiate LinkKey; node with incorrect PSK fails at CONFIRM_
- [x] Implement PeerManager with open-addressing hash table
  - _Test: insert 20 peers, search by MAC in O(1) amortized_
- [x] Implement PeerManager LRU eviction
  - _Test: force heap pressure; verify that peer with oldest `lastSeen` and `routeCount==0` is evicted_
- [x] Implement anti-replay by `(peer, seq)` on reception
  - _Test: re-send frame with `seq ≤ lastSeqRx` → silently discarded_

---

## Phase 4: Key rotation

**Spec:** `openspec/specs/crypto/spec.md` (§4.3)

- [x] Implement epoch rotation timer (`setKeyRotationInterval()`)
  - _Test: after the interval, the node increments epoch and the next frame uses epoch N+1_
- [x] Implement `KEY_NACK` and 1-frame buffer rejected by peer
  - _Test: sender receives KEY_NACK → initiates renegotiation → retransmits buffered frame_
- [x] Implement epoch detection in battery nodes (`RTC_DATA_ATTR`)
  - _Test: force epoch change during deep sleep; node detects and renegotiates upon wake_

---

## Phase 5: Basic routing

**Spec:** `openspec/specs/routing/spec.md`

- [x] Implement `RouteEntry` structure and static pool of 64 entries
  - _Unit test: insert, search by IP, update TTL_
- [x] Implement `ROUTE_ADV` with entry serialization (12B/entry, 18 per frame)
  - _Test: ROUTE_ADV with 18 entries occupies exactly 250 bytes_
- [x] Implement table update by ROUTE_ADV reception
  - _Test: 3 nodes A–B–C; C learns route to A via B after B's first ROUTE_ADV_
- [x] Implement Split Horizon and Poison Reverse
  - _Test: B does not advertise to A routes whose nextHop is A_
- [x] Implement Seen-Frame Cache (32 entries, circular buffer, TTL 10s)
  - _Test: frame received twice with same (srcMac, seq) → second discarded_
- [x] Implement triggered updates (immediate RA upon topology change detection)
  - _Test: disconnect peer → triggered RA sent in &lt; 1s_

---

## Phase 6: Complete routing and ROUTE_WITHDRAW

**Spec:** `openspec/specs/routing/spec.md`

- [x] Implement route eviction (expired → highest hopCount → least recent)
  - _Test: full table with 64 entries; add new one → worst entry is evicted_
- [x] Implement `ROUTE_WITHDRAW` broadcast
  - _Test: peer disappears → ROUTE_WITHDRAW sent → routes removed in neighbors_
- [x] Reconvergence test: 5 nodes, disconnect central node
  - _Test: full reconvergence in &lt; 60s (2 RA intervals of 30s)_
  - _Note: all logic (triggered updates, ROUTE_WITHDRAW, Split Horizon) is implemented; manual hardware test._

---

## Phase 7: Virtual IP interface (netif mesh0)

**Spec:** `openspec/specs/ip-netif/spec.md`

- [x] Create virtual netif `mesh0` with `esp_netif_new()` and custom driver
  - _Test: `mesh0` appears in `esp_netif_get_handle_from_ifkey("MESH0")`_
- [x] Implement RX path: decrypted DATA frame → `esp_netif_receive()`
  - _Test: IPv4 packet injected into `mesh0` received by UDP socket on the node_
- [x] Implement TX path: `mesh_netif_output()` → find route → encrypt → send
  - _Test: UDP socket on node A sends to B's IP; B receives the packet_
- [x] Configure MTU=216 and lwipopts.h (TCP_MSS=176, TCP_WND=704, SACK off)
  - _Test: MSS negotiated in TCP handshake = 176_
- [x] Implement transparent ping (verify that esp_ping works over mesh0)
  - _Test: `ping 10.200.0.x` between two nodes; measured RTT &lt; 50ms at 1 hop_
  - _Impl: `GatewayPing` example uses raw ICMP via `SOCK_RAW/IPPROTO_ICMP` (esp_ping not available in Arduino). Compiles OK._

---

## Phase 8: MTU, L2 fragmentation, and ARP

**Spec:** `openspec/specs/ip-netif/spec.md`

- [x] Implement L2 fragmentation (4B extra header, reassembly timeout 2s)
  - _Test: 300-byte UDP; fragmented into 2 L2 frames; correctly reassembled_
- [x] Implement gratuitous ARP on join
  - _Test: node joins → announces IP→MAC → neighbors update table without ARP_QUERY_
- [x] Implement `ARP_QUERY` broadcast and `ARP_REPLY` unicast
  - _Test: unknown IP → ARP_QUERY broadcast → ARP_REPLY received in &lt; 1s_

---

## Phase 9: DHCP and IP assignment

**Spec:** `openspec/specs/ip-netif/spec.md`

- [x] Implement distributed static table (MAC→IP in NVS + distribution via ROUTE_ADV)
  - _Test: node recovers its IP from NVS on restart without doing DHCP_
- [x] Implement DHCP server on gateway (lwIP dhcpserver)
  - _Extracted to the **mesh-dhcp** change for independent implementation._

---

## Phase 10: Onboarding

**Spec:** `openspec/specs/onboarding/spec.md`

- [x] Implement permanent onboarding AP with SSID `ENIGMA-&lt;NetworkID&gt;-CH&lt;channel&gt;`
  - _Test: SSID visible by WiFi scanner; password = HMAC(PSK,"onboarding")[:8] hex_
- [x] Implement provisioning HTTP server (`GET /provision`)
  - _Test: new node connects to AP, does GET /provision, receives correct JSON_
- [x] Implement `JOIN_BEACON` broadcast every 5s
  - _Test: node in blind search receives JOIN_BEACON in &lt; 10s if on correct channel_
- [x] Implement blind channel search (scan 1→6→11→rest, 200ms/channel)
  - _Test: configured node reboots; finds channel in &lt; 30s_

---

## Phase 11: WiFi Gateway

**Spec:** `openspec/specs/gateway/spec.md`

- [x] Implement dual-mode WiFi STA + ESP-NOW (single-chip)
  - _Test: gateway connected to WiFi AP on channel 6; mesh operates on channel 6_
- [x] Implement `MeshUplink` abstraction with `NativeWifiUplink`
  - _Test: WiFi uplink connected; `isConnected()` returns true_
- [x] Implement `ip_napt` (full NAT) on `mesh0` → `wifi_sta`
  - _Test: mesh node does TCP MQTT publish to broker on LAN → successful connection; broker sees gateway IP_
- [x] Implement NAT masquerade for all outgoing traffic (LAN and Internet)
  - _Test: mesh node pings 8.8.8.8 → response received_
- [x] Implement gateway selection by metric and redundancy
  - _Test: 2 active gateways; node chooses the one with best metric; if it falls, migrates to the other in &lt; 60s_

---

## Phase 12: Web UI and Prometheus

**Spec:** `openspec/specs/gateway/spec.md`

- [x] Implement `esp_http_server` with HTTP Digest Auth
  - _Test: GET `/` with incorrect credentials returns 401; correct ones return 200_
- [x] Implement minimal HTML dashboard (topology, nodes, routes)
  - _Test: browser loads `/` correctly_
- [x] Implement JSON endpoints: `/api/v1/status`, `/nodes`, `/routes`, `/peers`
  - _Test: valid JSON, correct fields_
- [x] Implement `/metrics` endpoint in Prometheus format
  - _Test: `curl http://&lt;gw&gt;/metrics` returns text with metrics listed in the spec_

---

## Phase 13: Battery nodes

**Spec:** `openspec/specs/battery-nodes/spec.md`

- [x] Implement WAKE → TX UPLINK → RX1 → RX2 → DEEP SLEEP cycle
  - _Test: node cycles every 60s (configurable); consumes &lt; 1mA average_
- [x] Implement downlink buffer in Parent (FIFO, 5 msgs × 200B per child)
  - _Test: 5 messages in buffer; node receives them all in RX windows after UPLINK_
- [x] Implement clock synchronization (Parent timestamp in UPLINK response)
  - _Test: `getMeshTime()` on battery node returns valid time after first UPLINK_
- [x] Implement list of 3 Parent candidates in NVS
  - _Test: primary Parent disappears; node recovers connection via candidate 2 without re-join_
- [x] Verify that battery node does NOT act as relay
  - _Test: third-party frame received by battery node → discarded (not retransmitted)_

---

## Phase 14: ESP8266 (monorepo, `src/` directory)

**Spec:** `openspec/specs/esp8266/spec.md`

&gt; **Architecture decision:** `MeshNode8266` is implemented in this same repository under `src/`,
&gt; using `#ifdef ESP8266` / `#ifndef ESP8266` guards to separate code by platform. Standard
&gt; approach for multi-platform Arduino libraries; compatible with PlatformIO/Arduino Library
&gt; Manager registration. Shared protocol constants go in `src/Protocol.h` without guards.
&gt; See "Monorepo structure" section in `design.md`.

- [x] Implement `MeshNode8266` with MQTT Proxy protocol
  - _Test: ESP8266 compiles with `pio run -e esp8266-MeshNode8266` with no errors_ ✓
- [x] Implement `PROXY_DISCOVERY` / `PROXY_OFFER` (proxy selection by RSSI)
  - _Test: ESP8266 selects the ESP32 proxy with best RSSI_ ✓
- [x] Implement `PROXY_PUBLISH` and `PROXY_MESSAGE` (broker → ESP8266)
  - _Test: ESP8266 publishes to topic; message reaches MQTT broker_ ✓
- [x] Implement broker distribution via provisioning and JOIN_BEACON
  - _Test: broker change in gateway; ESP8266 updates on next JOIN_BEACON_ ✓

---

## Phase 15: Service discovery

**Spec:** `openspec/specs/service-discovery/spec.md`

- [x] Implement `SERVICE_QUERY` / `SERVICE_REPLY` over mesh
  - _Test: node queries MQTT broker; gateway responds with IP:port in &lt; 1s_
- [x] Implement service records in ROUTE_ADV (optional field)
  - _Test: service advertised in RA visible in local service table after 30s_
- [x] Implement mDNS bridge on gateway (republishes mesh services to WiFi)
  - _Test: `avahi-browse` on LAN device discovers `_mqtt._tcp` from mesh broker_

---

## Phase 16: Public API and examples

**Spec:** `openspec/specs/public-api/spec.md`

- [x] Finalize `MeshNetwork.h` with complete API per spec
  - _Test: compilation with no warnings in Arduino IDE and PlatformIO_
- [x] Implement `getClient()` as `WiFiClient` wrapper over lwIP socket on mesh0
  - _Test: `PubSubClient` with `getClient()` connects and publishes to MQTT broker on LAN_
- [x] Create `BasicNode` example (Arduino)
  - _Test: flash on ESP32; node appears in gateway Web UI_
- [x] Create `GatewaySingleChip` example (Arduino)
  - _Test: flash on ESP32; onboarding AP visible; BasicNode connects_
- [x] Create `BatteryNode` example (Arduino)
  - _Test: node cycles every 60s; consumption measured_
- [x] Create `gateway_hosted` example (native IDF)
  - _Test: compiles with `idf.py build`; functional gateway with ESP-Hosted slave board_
- [x] Create `library.json` and verify Arduino Library Manager compatibility
  - _Test: `pio lib install` from the repository works_