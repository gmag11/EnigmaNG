# Spec: Routing (Distance Vector)

**Reference:** §6 of EnigmaNG Specs v2.md

## Purpose

Proactive multi-hop routing over the ESP-NOW mesh based on Distance Vector (RIPv2-like), with loop prevention using a Seen-Frame Cache + Split Horizon + Poison Reverse.

## Unified routing table (IP ↔ MAC ↔ NextHop)

```cpp
struct RouteEntry {
    uint32_t ip;          // 4B — destination IPv4
    uint8_t  mac[6];      // 6B — final destination MAC
    uint8_t  nextHop[6];  // 6B — next hop MAC
    uint8_t  hopCount;    // 1B — 0=local, 255=Poison Reverse
    int8_t   rssi;        // 1B — RSSI to nextHop
    uint32_t lastUpdate;  // 4B — millis()
    uint16_t ttl;         // 2B — seconds until expiry
    uint8_t  flags;       // 1B — IS_GATEWAY|IS_BATTERY|IS_DIRECT
};
// sizeof(RouteEntry) = 25 bytes
// Static pool: 64 entries = 1,600 bytes
```

## Route Advertisement (ROUTE_ADV)

- **Base interval:** `RA_INTERVAL` = 30s.
- **Triggered update:** topology change → immediate RA + timer reset.
- **Entry format (12 bytes each):**
  ```
  [IPv4: 4B][MAC_dest: 6B][HopCount: 1B][Flags: 1B]
  ```
  The `nextHop` is not included: it is the RA sender.
- **Capacity per frame:** 18 IPv4 entries (216B / 12B = 18 exact → 250B frame fits exactly).
- **Continuation:** If table > 18 entries, multiple RAs with `RA_CONTINUATION` flag.

## Route TTL

| Type | Default TTL |
|------|-------------|
| Direct neighbor | 90s |
| 2–4 hops | 180s |
| 5+ hops | 300s |

Preset `MESH_MOBILE_MODE`: TTL 90s for all + `RA_INTERVAL` = 15s.

## Loop prevention — Seen-Frame Cache

```cpp
struct SeenFrame {
    uint8_t  srcMac[6];   // 6B
    uint16_t seq;         // 2B
    uint32_t timestamp;   // 4B
};
// Circular buffer: 32 entries × 12B = 384 bytes per relay node
```

A relay discards the frame if `(srcMac, seq)` is in the cache (`SEEN_FRAME_TTL` = 10s). If not, it adds it and forwards the frame.

## Standard DVR mechanisms

1. **Split Horizon:** Do not advertise in RA toward peer X the routes with `nextHop == X`.
2. **Poison Reverse:** Routes learned from X are advertised to X with `hopCount = 255`.
3. **IP TTL:** lwIP decrements TTL; packets with TTL=0 are discarded.

## Route Withdraw

### Timeout and failure detection

| Node type | Timeout | Reason |
|---|---|---|
| Normal | 90s (`3 × RA_INTERVAL`) | 3 missing ROUTE_ADV |
| Battery | `max(3 × sleepInterval + 60s, 120s)` | May be legitimately sleeping |

The `sleepInterval` is announced by the battery node itself in the extra field of the `JOIN_BEACON` (bytes 6–9, `uint32_t` seconds). If the neighbor has not received that field, the 120s minimum applies.

### Behavior on expiry

When `_checkPeerTimeouts()` detects an expired peer:
1. Remove local routes to/through that peer (`handleRouteWithdraw(mac)`).
2. Invoke `onNodeLeave` callback.
3. **Emit `ROUTE_WITHDRAW` broadcast** with payload = 6-byte MAC of the lost peer.

### Receiving a ROUTE_WITHDRAW

On receiving a `ROUTE_WITHDRAW`:
1. If payload MAC equals own MAC → ignore.
2. If there are no routes toward that MAC → ignore (already converged, do not retransmit).
3. If routes exist → remove them and trigger `onNodeLeave` if it was a direct peer.

> **Note:** Do not retransmit received `ROUTE_WITHDRAW`. The original broadcast reaches
> all nodes in direct range of the emitter. Nodes that only learned it via multi-hop will converge by route expiry (max. additional 90s).

### Extended JOIN_BEACON for battery nodes

```
[channel: 1B][localIP: 4B][mode: 1B][sleepIntervalSec: 4B — only if mode==MESH_BATTERY]
```
Neighbors read this field on beacons and update `PeerEntry.sleepIntervalMs`.

## Memory control

- Static pool of `MAX_ROUTES` (default 64, `#define` at compile time).
- Eviction when table full: 1) expired entries → 2) highest hopCount → 3) least recently updated.

## Acceptance criteria

- Test: 3 nodes in line A–B–C. A sends DATA to C. Verify B forwards and C receives (1 frame, no duplicates).
- Test: 5 nodes, disconnect central node. Reconverge in < 60s (2 RA intervals).
- Test: Seen-Frame Cache discards duplicates when B receives the same frame via two paths.
- Test: frame with `hopCount = 255` does not create a route in the table.
- Test: ROUTE_WITHDRAW removes corresponding entries on all neighbors.
