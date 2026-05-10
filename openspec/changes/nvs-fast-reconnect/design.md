## Context

EnigmaNG nodes currently go through the full onboarding sequence (channel scan or AP provisioning, JOIN_BEACON, ECDH handshake) every time they boot or wake from deep sleep. For deployed nodes—especially battery nodes—this wastes time and energy: the mesh channel, the gateway MAC, and per-peer link keys are all stable across reboots unless the network is reconfigured.

The ESP32 NVS (Non-Volatile Storage) provides a CRC-checked, wear-levelled key-value store that survives deep sleep and resets. A small, versioned cache of connection state persisted there allows a node to skip onboarding entirely in the normal case.

## Goals / Non-Goals

**Goals:**
- Persist connection state (channel, gateway MAC, NetworkID, gateway route next-hop + hop count, per-peer link key + epoch) to NVS after a successful join.
- On boot/wake, restore that state and enter connected mode without a channel scan or JOIN_BEACON exchange.
- Handle stale cache gracefully: if the peer rejects a frame (KEY_NACK / no link-key record), transparently re-establish the session and retransmit the buffered message.
- Keep the change invisible to the sketch author (no new API surface required).

**Non-Goals:**
- Persisting the full routing table or service-discovery records. Only the single gateway route is cached.
- Modifying the battery-node parent-candidate NVS logic (already specced in `battery-nodes`).
- Encrypting NVS entries beyond what the IDF NVS partition already provides.
- Supporting ESP8266 (no NVS API available in the Arduino target).

## Decisions

### Decision 1 — NVS namespace and key layout

**Choice:** Use a single NVS namespace `enigma_cc` (connection cache) with the following keys:

| Key | Type | Size | Content |
|-----|------|------|---------|
| `cc_ver` | `uint8_t` | 1 B | Schema version (current = 1). Invalidates cache on upgrade. |
| `cc_ch` | `uint8_t` | 1 B | WiFi/ESP-NOW channel. |
| `cc_nid` | `blob` | 2 B | NetworkID. |
| `cc_gwmac` | `blob` | 6 B | Gateway MAC. |
| `cc_gwrt` | `blob` | 7 B | Gateway route: `nextHop[6]` + `hopCount[1]`. |
| `cc_peers` | `blob` | N × 23 B | Array of `CachedPeer` structs (see below). |

```cpp
struct CachedPeer {
    uint8_t mac[6];      // 6 B
    uint8_t linkKey[16]; // 16 B — AES-128
    uint8_t epoch;       // 1 B
};  // 23 bytes each
```

Maximum peers cached: `MESH_MAX_PEERS` (default 8) → `cc_peers` blob ≤ 184 B.

**Rationale:** A flat blob for peers is simpler than per-peer keys and avoids NVS key-count limits. The version byte allows cache invalidation on firmware upgrades.

The gateway route (`cc_gwrt`) is the only routing entry cached. Without it, a fast-booting node cannot send any frame until the first `ROUTE_ADV` arrives (up to `RA_INTERVAL` = 30 s in the worst case). Caching the full routing table is unnecessary: the rest of the table is rebuilt within seconds once the first triggered `ROUTE_ADV` propagates after the node emits its first frame.

**Alternative considered:** One NVS key per peer keyed by MAC. Rejected because MAC-addressed keys would require up to 8 separate NVS entries, complicating iteration and atomic update.

### Decision 2 — Cache write strategy

**Choice:** Write on every `setLinkKey()` call (i.e., after each successful ECDH handshake) and on every epoch rotation. Writes are deferred 500 ms via a dirty flag to coalesce back-to-back handshakes during initial join.

**Rationale:** Link-key updates are infrequent (default rotation every 24 h). The NVS write latency (~2–10 ms) is acceptable within this budget. Coalescing avoids NVS wear during the brief burst of handshakes right after boot.

**Alternative considered:** Write only on graceful shutdown / `end()`. Rejected because a crash or power-cut before shutdown would leave an invalid cache from a partial join.

### Decision 3 — Fast-boot path in `Onboarding`

**Choice:** At the start of `begin()`, attempt to load the NVS cache. If the cache version matches and the channel, NetworkID, and gateway MAC are non-zero, skip channel scan and AP provisioning: set the channel directly on `PhysicalLayer`, pre-populate `PeerManager` with cached peers, and emit the first UPLINK / data frame without a JOIN_BEACON.

Fallback to normal onboarding if:
- Cache is absent or version mismatches.
- The first outbound frame is rejected by all known peers (no ACK within `FAST_BOOT_TIMEOUT_MS` = 4 000 ms).
- The node is in `MESH_GATEWAY` mode (gateway always runs full onboarding to refresh AP provisioning data).

**Rationale:** This mirrors the battery-node parent-candidate logic but generalises it to all node modes. Gateways are excluded because they own the provisioning AP and must always be authoritative.

### Decision 4 — Expired link key recovery on fast-boot

**Choice:** No new frame type is introduced. The existing `KEY_NACK` + buffer-and-retransmit flow (already implemented in `MeshNetwork::_nackBuf`) covers the primary fast-boot failure case: the peer recognises the node but the cached link key is expired. The peer sends `KEY_NACK`; the node renegotiates and retransmits the buffered frame transparently.

**Unknown peer after fast-boot:** if the peer has also rebooted and lost its `PeerEntry`, the DATA frame is silently dropped. The sender detects this via the `FAST_BOOT_TIMEOUT_MS` (4 s) watchdog and falls back to full onboarding. No explicit rejection frame is sent to avoid providing a denial-of-service amplification vector for misbehaving nodes.

**Wrong PSK:** KEY_EXCH_HELLO from a node with an incorrect PSK fails the NetworkKey GCM check and is silently discarded before any state is touched. Consistent with the existing policy.

**Rationale:** The KEY_NACK path already handles the most common case (expired key). The fast-boot timeout cleanly handles the rare case (peer also rebooted). Adding a `JOIN_REJECT` frame type would create a new attack surface and an amplification risk if a rogue node sends floods of DATA frames — the current silent-drop behaviour is the correct defence.

## Risks / Trade-offs

| Risk | Mitigation |
|------|-----------|
| NVS corruption (CRC error) | IDF NVS handles CRC internally; on error, `nvs_get_blob` returns `ESP_ERR_NVS_INVALID_CRC`. The code falls back to normal onboarding. |
| Stale channel in cache after network reconfiguration | `FAST_BOOT_TIMEOUT_MS` = 4 s timeout: if no peer responds, fall back to channel scan and invalidate cache on success. |
| Flash wear from frequent key-rotation writes | Key rotation default is 24 h → ~365 writes/year per peer, well within NVS wear-levelling budget. |
| Two nodes reboot simultaneously, neither has a record of the other | Both fall back to normal JOIN_BEACON flow; cache is rebuilt after the first successful handshake. |
| Fast-boot node unknown to peer (peer also rebooted) | 4 s `FAST_BOOT_TIMEOUT_MS` watchdog → invalidate cache → full onboarding. Normal case: at least one peer survives. |

## Migration Plan

1. On first boot after firmware update, `cc_ver` will be absent → `nvs_get` returns `ESP_ERR_NVS_NOT_FOUND` → normal onboarding runs. No migration needed.
2. If the version byte changes in a future update, the cache is invalidated and rebuilt automatically.
3. No NVS partition resizing required: total new NVS usage ≤ 1 + 1 + 2 + 6 + 7 + 184 = **201 B** (plus ~20 B NVS overhead per key = ≤ 360 B total).

## Open Questions

- Should the cache be invalidated when `setNetworkKey()` is called at runtime (PSK change)? Current assumption: yes — a PSK change implies a different `NetworkID`, which is checked at fast-boot.
- Should battery nodes share this cache with the existing parent-candidate NVS entries, or keep separate namespaces? Current assumption: separate namespaces to avoid coupling.
