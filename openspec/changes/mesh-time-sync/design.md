## Context

EnigmaNG nodes currently have no wall-clock time unless the application sets it externally. `getMeshTime()` is a thin wrapper around `time(nullptr)` (returns 0 at boot). The gateway, once its WiFi uplink connects, is the only mesh member that can reach the internet and therefore NTP.

The existing codebase has scaffolding for time sync (`MeshTimeCallback`, `_onTimeSyncCb`, `BatteryState::lastMeshTime`) but the actual synchronization mechanism is not implemented. The goal is to close that gap using what is already there, adding minimal new protocol surface.

**Constraint: 216-byte MTU.** All mesh frames are encrypted ESP-NOW (MTU 250 bytes, 34 bytes overhead = 216 bytes payload). The time-sync message must fit easily in one unencrypted or encrypted frame.

**Constraint: no multi-hop for v1.** A node can only sync time with a peer it has an active encrypted session with. Nodes that are connected directly to the gateway sync to the gateway; nodes that can only reach intermediate relays do not get time sync in this version.

## Goals / Non-Goals

**Goals:**
- Gateway acquires UTC time via SNTP when the WiFi uplink connects.
- A connected node can request time from the gateway peer with a single call (`mesh.syncTime()`).
- The system clock is set via `settimeofday()` so `time()`, `localtime()`, `gettimeofday()` work transparently in application code.
- RTT compensation: the node subtracts half the observed round-trip time to reduce the clock offset introduced by transmission latency.
- Battery nodes restore their clock from RTC memory on wake (before the first `TIME_REQ`).
- Gateway re-broadcasts time periodically (~10 min) and on each new node join.
- Configurable NTP servers and POSIX timezone string via `enableNTP()`.

**Non-Goals:**
- Sub-millisecond precision.
- Multi-hop time propagation (node → relay → far node).
- NTP on nodes (nodes are clients of the gateway only).
- ESP8266 / `MeshNode8266` support.
- Persistent NTP config in NVS (out of scope v1).

## Decisions

### 1. Frame type: new `TIME_SYNC` and `TIME_REQ` control frames

Two new `FrameType` values added to `LinkLayer.h`:

```
TIME_REQ  = 0x10   // Node → Gateway: request current time
TIME_SYNC = 0x11   // Gateway → Node: deliver current time
```

Both use `Protocol::MESH_INTERNAL` and are encrypted (sent with `_sendFrame` to the gateway peer or as unicast reply).

**Why not reuse `CONTROL`?** `CONTROL` has no sub-type field in the current spec; adding one would require touching existing frame parsing. Dedicated frame types are cleaner and keep parsing O(1).

**TIME_REQ payload (4 bytes):**
```
uint32_t  tx_ms;   // sender's millis() at send time (for RTT)
```

**TIME_SYNC payload (12 bytes):**
```
uint32_t  unix_sec;   // UTC seconds since epoch
uint16_t  unix_ms;    // milliseconds part
uint32_t  req_tx_ms;  // echo of TIME_REQ.tx_ms (0 if unsolicited broadcast)
uint16_t  gw_proc_ms; // gateway processing delay in ms (for RTT correction)
```

Total payload: 12 bytes. Well within the 216-byte limit.

### 2. RTT compensation algorithm

On receiving `TIME_SYNC` (unicast response to `TIME_REQ`):

```
rtt_ms      = millis() - req_tx_ms
flight_ms   = (rtt_ms - gw_proc_ms) / 2
corrected_t = unix_sec + (unix_ms + flight_ms) / 1000
```

This matches the classic NTP offset formula simplified for one-way asymmetric networks. For broadcast `TIME_SYNC` (unsolicited), `req_tx_ms = 0` — no RTT correction applied; accuracy is ±typical_latency (~3–15 ms over ESP-NOW).

### 3. IDF SNTP on gateway

Use the IDF `esp_sntp` API (available in Arduino-ESP32 as `esp_sntp.h`):

```cpp
esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
esp_sntp_setservername(0, "pool.ntp.org");
esp_sntp_init();
```

Called from the `ARDUINO_EVENT_WIFI_STA_GOT_IP` handler that already exists in `MeshNetwork.cpp`. The callback `sntp_set_time_sync_notification_cb()` fires when the clock is first set; the gateway then broadcasts a `TIME_SYNC` to all known peers.

**Why IDF SNTP and not Arduino's `configTime()`?** `configTime()` is a wrapper around IDF SNTP — using IDF directly allows registering the sync callback without polling. Functionally equivalent; IDF API is more explicit.

### 4. `enableNTP()` public API

```cpp
// Gateway only. Must be called before begin() or immediately after.
void enableNTP(const char* server1 = "pool.ntp.org",
               const char* server2 = "time.cloudflare.com",
               const char* tz      = "UTC0");
```

If not called, defaults are applied automatically when the gateway initialises SNTP. `tz` is a POSIX TZ string (e.g. `"CET-1CEST,M3.5.0,M10.5.0/3"`); passed to `setenv("TZ", tz, 1)` + `tzset()`.

### 5. `syncTime()` / `isTimeSynced()` public API

```cpp
bool syncTime();          // Send TIME_REQ to gateway peer; false if no gateway peer yet
bool isTimeSynced();      // true after settimeofday() has been called at least once
```

`onTimeSync(cb)` callback fires with the corrected `time_t` after `settimeofday()`.

### 6. Battery node clock persistence

On receiving a valid `TIME_SYNC` (or completing `syncTime()`), `BatteryNode::setMeshTime(t)` stores `t` into `BatteryState::lastMeshTime` in RTC memory. On wake, before attempting `TIME_REQ`, the node calls `settimeofday()` from RTC memory if `lastMeshTime > 1_000_000_000` (i.e. sane epoch value), giving it an approximate clock immediately. The error is bounded by the sleep interval (configurable, default ≤ 60 s).

### 7. Periodic re-broadcast from gateway

After first NTP sync, gateway sends `TIME_SYNC` broadcast every `_ntpBroadcastIntervalMs` (default 600 000 ms = 10 min). Also sent unicast to each new node on handshake complete (`_onPeerHandshakeComplete`).

## Risks / Trade-offs

- **Clock drift between syncs.** ESP32 crystal drift is ~10–100 ppm. Over 10 min this is < 60 ms. Acceptable for logging and scheduling; not for sub-second precision applications.
  → Mitigation: users can reduce `_ntpBroadcastIntervalMs` via a compile-time define or future API.

- **Gateway not yet NTP-synced when node joins.** The gateway may join before SNTP completes (SNTP can take 1–5 s after uplink connect).
  → Mitigation: if `isTimeSynced()` is false on the gateway, the `TIME_SYNC` frame is not sent. The node will retry via `syncTime()` or wait for the periodic broadcast.

- **Single-hop limitation.** Nodes beyond one hop from the gateway cannot sync in v1.
  → Mitigation: documented as known limitation. Relay-based propagation can be added in v2 by having intermediate nodes forward `TIME_SYNC` with updated RTT fields.

- **`settimeofday()` jump.** Adjusting the clock by more than ~1 s can confuse applications that assume monotonic time.
  → Mitigation: use `adjtime()` for small corrections (< 2 s). Only jump with `settimeofday()` for first sync or large corrections. This is standard NTP behaviour.

## Migration Plan

This change adds new frame types and new public API. It is purely additive:

1. Deploy updated gateway firmware first — it starts serving `TIME_SYNC` but old nodes ignore unknown frame types safely (frame type not in their switch-case → discard).
2. Deploy updated node firmware — nodes start sending `TIME_REQ` and processing `TIME_SYNC`.
3. No NVS migration needed.
4. Rollback: revert to previous firmware. Old and new firmware interoperate (unknown frames are discarded).

## Open Questions

- Should `TIME_SYNC` broadcasts be sent over the encrypted unicast channel to each peer, or as a broadcast frame? Broadcast frames are cheaper but broadcast encryption is not implemented. → **Decision**: unicast to each established peer, triggered on NTP sync callback and on periodic timer.
- Should `enableNTP()` accept a `uint32_t intervalMs` parameter for the broadcast interval? → Deferred to v2; use compile-time define for v1.
