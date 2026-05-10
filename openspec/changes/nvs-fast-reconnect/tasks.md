## 1. NVS Cache Data Layer (`ConnectionCache`)

- [ ] 1.1 Add `ConnectionCache.h` / `ConnectionCache.cpp` to `src/`. Define `CachedPeer` struct (mac[6] + linkKey[16] + epoch = 23 B) and `ConnectionCache` class with `save()`, `load()`, `invalidate()` methods using IDF NVS API under namespace `enigma_cc`. _Test: unit-test save then load round-trips all fields correctly._
- [ ] 1.2 Implement `save()`: write `cc_ver` (uint8 = 1), `cc_ch` (uint8), `cc_nid` (blob 2 B), `cc_gwmac` (blob 6 B), `cc_gwrt` (blob 7 B: `nextHop[6]` + `hopCount[1]`), `cc_peers` (blob ≤ `MESH_MAX_PEERS` × 23 B) to NVS. Apply 500 ms deferred write via dirty flag + `millis()`. _Test: verify dirty flag prevents double-write within 500 ms window._
- [ ] 1.3 Implement `load()`: read and validate all keys; return `false` if any read returns `ESP_ERR_NVS_NOT_FOUND`, `ESP_ERR_NVS_INVALID_CRC`, or `cc_ver ≠ 1`. _Test: corrupt one NVS key → `load()` returns false._
- [ ] 1.4 Implement `invalidate()`: erase all `enigma_cc` keys via `nvs_erase_all()` on the namespace handle. _Test: after `invalidate()`, `load()` returns false._
- [ ] 1.5 Guard all `ConnectionCache` code with `#if !defined(ESP8266)` to exclude from ESP8266 builds. _Test: ESP8266 build compiles without NVS symbols._

## 2. PeerManager Integration

- [ ] 2.1 Add `saveToCacheEntry(CachedPeer&)` and `loadFromCacheEntry(const CachedPeer&)` helpers to `PeerManager`. _Test: round-trip a `PeerEntry` through `CachedPeer` preserves mac, linkKey, and epoch._
- [ ] 2.2 Call `ConnectionCache::save()` (deferred) at the end of `PeerManager::setLinkKey()` after updating the entry. _Test: after `setLinkKey()`, wait 600 ms, reload from NVS — entry present._
- [ ] 2.4 Hook into `Router` to call `ConnectionCache::saveGatewayRoute(nextHop, hopCount)` whenever the gateway route changes (new next-hop or hop count). _Test: after a route update to the gateway, wait 600 ms, reload from NVS — `cc_gwrt` reflects the new next-hop._
- [ ] 2.3 Expose `PeerManager::restoreFromCache(const ConnectionCache&)` that bulk-inserts `CachedPeer` entries as pre-seeded `PeerEntry` records with `keyEstablished = true`. _Test: restore 3 cached peers → `findPeer()` succeeds for all 3 MACs._

## 3. Onboarding Fast-Boot Path

- [ ] 3.1 At the start of `Onboarding::begin()` (or `MeshNetwork::begin()`), attempt `ConnectionCache::load()`. If successful and mode ≠ `MESH_GATEWAY`, set ESP-NOW channel via `PhysicalLayer::setChannel()`, call `PeerManager::restoreFromCache()`, inject the cached gateway route into `Router` (next-hop + hop count, TTL = 90 s), and set a `_fastBootActive` flag. Skip channel scan and JOIN_BEACON. _Test: node with valid cache skips WiFi scan and has a valid gateway route entry in `Router` immediately after `begin()`._
- [ ] 3.2 Start `FAST_BOOT_TIMEOUT_MS` (4 000 ms) timer when `_fastBootActive` is true. If no peer ACKs within the window, call `ConnectionCache::invalidate()`, clear `_fastBootActive`, and begin normal onboarding. _Test: mock all peers as silent → after 4 s node enters full onboarding._
- [ ] 3.3 Cancel the fast-boot timer as soon as the first peer ACK is received. _Test: peer ACKs at t=500 ms → fast-boot timer is cancelled, normal onboarding is NOT triggered._
- [ ] 3.4 Ensure `MESH_GATEWAY` mode always skips fast-boot regardless of cache state. _Test: gateway with valid NVS cache → fast-boot path not entered, full AP provisioning runs._

## 4. KEY_NACK Recovery on Fast-Boot (verify existing implementation)

- [ ] 4.1 Confirm that `_nackBuf` in `MeshNetwork` stores the rejected frame and retransmits it after `KEY_EXCH_CONFIRM`. No code change expected; add a test that covers the fast-boot scenario. _Test: node fast-boots with expired cached link key → peer sends KEY_NACK → node renegotiates → original DATA frame is delivered._
- [ ] 4.2 On `KEY_EXCH_HELLO` timeout for a fast-booted peer: call `ConnectionCache::invalidate()` so the stale cache entry does not persist. _Test: KEY_EXCH_HELLO times out → NVS cache is erased → next boot runs full onboarding._

## 4. Cache Invalidation on PSK Change

- [ ] 4.3 In `MeshNetwork::setNetworkKey()`, after computing the new NetworkID, compare with the cached `cc_nid`. If different, call `ConnectionCache::invalidate()`. _Test: call `setNetworkKey()` with a new PSK → NVS cache is erased._

## 5. Tests

- [ ] 5.1 Add unit test `test_nvs_connection_cache`: covers save/load/invalidate/version-mismatch/CRC-error/gateway-route scenarios. _Test: all scenarios in `nvs-connection-cache` spec pass._
- [ ] 5.2 Extend `test_onboarding` (or create it): covers fast-boot success, fast-boot timeout (unknown peer), gateway bypass. _Test: all onboarding spec scenarios pass._
- [ ] 5.3 Extend `test_key_exchange` or `test_link_layer`: fast-boot with expired link key → KEY_NACK → retransmit; wrong PSK silently discarded. _Test: all crypto spec scenarios pass._
