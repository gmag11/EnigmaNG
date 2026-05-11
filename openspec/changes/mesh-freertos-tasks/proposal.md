## Why

Currently, `MeshNetwork::_onFrameReceived()` executes directly in the ESP-NOW/WiFi task context. Heavy operations (ECDH key exchange, AES-GCM decryption, routing updates) performed there risk triggering the watchdog and interfering with WiFi stability. Additionally, the periodic maintenance work (`_sendJoinBeacon`, `_sendRouteAdv`, `_checkPeerTimeouts`) depends on the application calling `loop()` regularly — a contract that breaks silently when the application does blocking I/O (MQTT reconnect, HTTP requests, etc.), causing beacon loss and peer timeouts.

## What Changes

- **New**: A FreeRTOS `MeshRxTask` dequeues received frames from a `QueueHandle_t` and processes them (crypto, routing, netif injection) outside the WiFi task.
- **New**: A FreeRTOS `MeshMaintenanceTask` runs periodic mesh housekeeping (beacons, route advertisements, peer timeout checks, epoch rotation) on a fixed interval, independent of the application loop.
- **New**: A `SemaphoreHandle_t` (recursive mutex) protects shared state (PeerManager, Router, Fragmentation) accessed from both tasks.
- **Modified**: `MeshNetwork::begin()` creates and starts both FreeRTOS tasks.
- **Removed**: `MeshNetwork::loop()` is removed — maintenance is now driven by the FreeRTOS task, not by the application.
- **Modified**: `MeshNetwork::shutdown()` deletes both tasks and releases the mutex. **BREAKING**
- The `_onFrameReceived` static callback now only enqueues raw frames onto the RxQueue — no processing.

## Capabilities

### New Capabilities

- `mesh-rx-task`: FreeRTOS task that dequeues and fully processes received mesh frames outside the WiFi/ESP-NOW task context.
- `mesh-maintenance-task`: FreeRTOS task that drives all periodic mesh maintenance (beacons, route advertisements, peer timeouts, epoch rotation) at a fixed cadence.

### Modified Capabilities

- `public-api`: `begin()` now starts background tasks; `loop()` is removed. **BREAKING** — callers must remove `loop()` calls from their application code.

## No-Goals

- No changes to the ESP8266 build path (`#if !defined(ESP8266)` guard remains).
- No changes to the wire protocol, frame format, or cryptographic algorithms.
- No changes to the `NetifDriver` or lwIP integration.
- Backward compatibility shim for `loop()` is out of scope — callers must be updated.
- Battery node deep-sleep sequencing is out of scope.

## Impact

- **`src/MeshNetwork.h`**: Add `QueueHandle_t _rxQueue`, `TaskHandle_t _rxTask`, `TaskHandle_t _maintTask`, `SemaphoreHandle_t _mutex`.
- **`src/MeshNetwork.cpp`**: Refactor `begin()`, `loop()`, `shutdown()`, `_onFrameReceived()`; add static task entry functions.
- **RAM**: ~6 KB additional (two task stacks + queue item buffer).
- **Dependencies**: FreeRTOS (already present via IDF/Arduino Core).
- **Examples**: `IperfNode`, `GatewaySingleChip`, `MqttNode` etc. must remove their `mesh.loop()` calls — **breaking change** for all consumers.
