## ADDED Requirements

### Requirement: NVS connection cache persistence
The system SHALL persist the following connection state to NVS under the namespace `enigma_cc` after a successful network join, and update it whenever the state changes:
- Cache schema version (`cc_ver`, uint8, current = 1).
- The active ESP-NOW channel (`cc_ch`, uint8).
- The 2-byte NetworkID (`cc_nid`, blob).
- The gateway MAC address (`cc_gwmac`, blob, 6 bytes).
- The route to the gateway (`cc_gwrt`, blob, 7 bytes): `nextHop[6]` + `hopCount[1]`.
- An array of `CachedPeer` structs (`cc_peers`, blob, up to `MESH_MAX_PEERS` entries × 23 bytes each):
  ```
  struct CachedPeer { uint8_t mac[6]; uint8_t linkKey[16]; uint8_t epoch; };
  ```
Writes SHALL be deferred by 500 ms after the last state change to coalesce back-to-back updates.

#### Scenario: State saved after successful join
- **WHEN** a node completes the JOIN_BEACON handshake and at least one ECDH handshake successfully
- **THEN** the system SHALL write channel, NetworkID, gateway MAC, gateway route (next-hop + hop count), and all established link keys to `enigma_cc` within 500 ms

#### Scenario: Gateway route updated on routing table change
- **WHEN** the routing table entry for the gateway changes (different next-hop or hop count)
- **THEN** the system SHALL update `cc_gwrt` in NVS within 500 ms

#### Scenario: Cache updated after key rotation
- **WHEN** a link key is renegotiated (new epoch)
- **THEN** the system SHALL update the corresponding `CachedPeer` entry in `cc_peers` within 500 ms

#### Scenario: Cache invalidated on schema version mismatch
- **WHEN** the firmware reads `cc_ver` and the value does not equal the current schema version
- **THEN** the system SHALL discard the entire cache and fall back to normal onboarding

#### Scenario: Cache invalidated on NVS CRC error
- **WHEN** any NVS read returns `ESP_ERR_NVS_INVALID_CRC` or `ESP_ERR_NVS_NOT_FOUND`
- **THEN** the system SHALL discard the cache and fall back to normal onboarding

---

### Requirement: Fast-boot from NVS cache
On boot or wake from deep sleep, the system SHALL attempt to restore connection state from the NVS cache before initiating onboarding. If the cache is valid, the system SHALL also inject the cached gateway route (`cc_gwrt`) into the routing table so that the node can send frames immediately without waiting for a `ROUTE_ADV`. The fast-boot path SHALL be skipped for nodes in `MESH_GATEWAY` mode.

#### Scenario: Successful fast-boot
- **WHEN** a non-gateway node boots and finds a valid NVS cache (correct version, non-zero channel and gateway MAC)
- **THEN** the system SHALL configure the ESP-NOW channel directly from the cache, pre-populate `PeerManager` with cached peers, and emit the first data frame without running a channel scan or JOIN_BEACON exchange

#### Scenario: Cached gateway route injected into routing table on fast-boot
- **WHEN** a node fast-boots from a valid NVS cache
- **THEN** the system SHALL insert a `RouteEntry` for the gateway using the cached `nextHop` and `hopCount`, with a TTL of 90 s, so the node can send the first frame before any `ROUTE_ADV` is received

#### Scenario: Fast-boot timeout falls back to normal onboarding
- **WHEN** fast-boot is attempted and no cached peer acknowledges a frame within `FAST_BOOT_TIMEOUT_MS` (4 000 ms)
- **THEN** the system SHALL invalidate the NVS cache and run the normal onboarding sequence

#### Scenario: Gateway always runs full onboarding
- **WHEN** a node in `MESH_GATEWAY` mode boots
- **THEN** the system SHALL skip the fast-boot path and run full onboarding regardless of cache state

---

### Requirement: NVS cache invalidation on PSK change
The system SHALL invalidate the NVS connection cache whenever `setNetworkKey()` is called at runtime with a different PSK, because the NetworkID derived from the new PSK will differ.

#### Scenario: PSK changed at runtime
- **WHEN** `setNetworkKey()` is called with a PSK that produces a different NetworkID than the cached one
- **THEN** the system SHALL erase all `enigma_cc` NVS keys and run full onboarding on next boot
