## Why

Nodes in the mesh have no reliable wall-clock time: `getMeshTime()` currently delegates to `time(nullptr)`, which returns epoch 0 unless the system clock has been set externally. The gateway, once connected to WiFi, can reach NTP and has an accurate clock. Distributing that time over the mesh allows every node to use standard IDF/POSIX time functions (`time()`, `localtime()`, `gettimeofday()`) with real timestamps — useful for logging, certificates, scheduled actions, and sensor timestamping.

The existing `BatteryNode` already stores `lastMeshTime` across deep-sleep cycles in RTC memory, and the handshake clock-sync mechanism (embedded in the mesh join sequence) provides a lightweight sub-second round-trip-time (RTT)-compensated timestamp exchange. These can be leveraged to keep accuracy without a separate protocol.

## What Changes

- **Gateway** starts SNTP (IDF `esp_sntp`) automatically once the WiFi uplink is connected; configurable NTP servers and timezone.
- **New control frame** `TIME_SYNC` (one-shot, unicast + broadcast): carries a 64-bit Unix timestamp (seconds + milliseconds) and a gateway-side tx-timestamp for RTT compensation.
- **Node time synchronization**: on receiving `TIME_SYNC`, the node calls `settimeofday()` with the RTT-corrected timestamp, making `time(nullptr)` return real wall-clock time.
- **On-demand request**: nodes can send `TIME_REQ` to the gateway peer; gateway replies with `TIME_SYNC`. This is the mechanism behind `mesh.syncTime()`.
- **`MeshNetwork::syncTime()`**: new public API to request time sync explicitly. Returns immediately; result arrives via `onTimeSync` callback.
- **`MeshNetwork::enableNTP()`**: gateway-only; configures NTP servers and timezone. Called automatically with defaults if not configured before `begin()`.
- **`BatteryNode`**: `TIME_SYNC` also updates `BatteryState::lastMeshTime`, persisting the clock across deep-sleep cycles.
- **`getMeshTime()`** stays as-is (`time(nullptr)`) — once the system clock is set it returns the correct value transparently.

## Capabilities

### New Capabilities

- `time-sync`: Mesh-internal time synchronization protocol. Covers the `TIME_SYNC` and `TIME_REQ` control frames, the RTT compensation algorithm, `syncTime()` API, and `enableNTP()` gateway configuration.

### Modified Capabilities

- `public-api`: Add `syncTime()`, `enableNTP(ntp1, ntp2, tz)`, and `isTimeSynced()` to `MeshNetwork`. Extend `MeshTimeCallback` signature with accuracy estimate.
- `gateway`: NTP initialization on uplink connect; propagate time to mesh on first sync and periodically thereafter.
- `battery-nodes`: Persist synced time in `BatteryState::lastMeshTime`; restore system clock from RTC memory on wake before first `TIME_REQ`.

## Impact

- **`src/MeshNetwork.cpp/.h`**: new frame type handling, `syncTime()`, `enableNTP()`, periodic time broadcast from gateway.
- **`src/LinkLayer.h`**: add `FrameType::TIME_SYNC = 0x10` and `FrameType::TIME_REQ = 0x11`.
- **`src/BatteryNode.cpp/.h`**: persist/restore clock in `BatteryState`.
- **`src/Gateway.cpp`**: trigger NTP init and first time-broadcast on `ARDUINO_EVENT_WIFI_STA_GOT_IP`.
- **No new external dependencies**: IDF's built-in `esp_sntp` component is used; no third-party library.
- **No breaking changes** to existing public API.

## No-goals

- Sub-millisecond precision (GPS or PTP-grade accuracy).
- Time distribution over more than one hop (nodes sync directly to the gateway; multi-hop propagation is out of scope for v1).
- ESP8266 `MeshNode8266` support (no IP stack; out of scope).
