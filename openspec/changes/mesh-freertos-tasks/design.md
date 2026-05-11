## Context

EnigmaNG's `MeshNetwork::_onFrameReceived()` is a static callback invoked directly by QuickESPNow, which runs in the ESP-IDF `wifi` task (priority 23, stack ~6 KB). Any work done there—ECDH key exchange (Curve25519), AES-128-GCM decryption, routing table updates—delays the WiFi task and risks triggering the task watchdog.

The periodic mesh maintenance (`_sendJoinBeacon`, `_sendRouteAdv`, `_checkPeerTimeouts`, `_rotateEpoch`) currently relies on the application calling `MeshNetwork::loop()`. If the application blocks (MQTT reconnect, HTTP request), beacons are delayed and peers time out. Removing this contract requires an independent timer-driven task.

Shared mutable state (PeerManager, Router, Fragmentation, HandshakeContext array, NackBuffer) is currently accessed only from `loop()` and from `_onFrameReceived()` callbacks — both sequentially on the Arduino main task. Moving to two parallel tasks requires explicit mutual exclusion.

## Goals / Non-Goals

**Goals:**
- Process received frames outside the WiFi task context.
- Drive periodic maintenance at a fixed cadence, independent of the application.
- Protect shared mesh state with a recursive mutex.
- Remove `loop()` from the public API.
- Keep the wire protocol, frame format, and crypto algorithms unchanged.
- Keep the ESP8266 build path (`#if !defined(ESP8266)`) unaffected.

**Non-Goals:**
- Backward compatibility shim for `loop()`.
- Changes to NetifDriver or lwIP integration.
- Battery node deep-sleep sequencing.
- Multi-core pinning (let the scheduler decide).

## Decisions

### D1 — RxQueue item format: raw bytes vs. parsed struct

**Decision**: Store raw frame bytes + metadata (srcMac, len, rssi) in the queue item.

**Rationale**: Parsing inside the WiFi callback would still execute in the WiFi task context, providing no benefit. Raw copy is the minimal safe operation. Queue item size is bounded by the maximum ESP-NOW payload (250 bytes) plus 6 (MAC) + 1 (rssi) + 1 (len) = **258 bytes per item**.

**Alternative considered**: Allocate heap per frame and store pointer. Rejected — heap fragmentation risk on long-running embedded systems; fixed-size items are safer and allocation-free.

### D2 — Queue depth

**Decision**: 8 items (≈ 2 KB of queue storage).

**Rationale**: ESP-NOW delivers at most one frame per peer per 250 ms window under normal traffic. A depth of 8 absorbs burst traffic from up to 8 peers in a single maintenance cycle without dropping frames. Deeper queues provide diminishing returns and cost more RAM.

### D3 — Mutex type: recursive vs. non-recursive

**Decision**: Recursive mutex (`xSemaphoreCreateRecursiveMutex`).

**Rationale**: `_handleFrame()` may call `_sendFrame()` → `_initiateHandshake()` → `_allocHandshake()` in a single call chain, all of which touch shared state. A recursive mutex avoids deadlock in these re-entrant paths without restructuring the call graph.

### D4 — MeshRxTask priority and stack

**Decision**: Priority `(tskIDLE_PRIORITY + 5)`, stack **6 144 bytes**.

**Rationale**: Must run above `tskIDLE_PRIORITY` to preempt idle, but below the WiFi task (23) so ESP-NOW delivery is never blocked by frame processing. Stack sized for worst-case ECDH + AES-GCM path (measured peak ~4.5 KB on ESP32; 6 KB adds margin).

### D5 — MeshMaintenanceTask period and stack

**Decision**: 10 ms tick period, priority `(tskIDLE_PRIORITY + 3)`, stack **3 072 bytes**.

**Rationale**: Beacons and route advertisements use millis()-based counters internally (hundreds of ms to seconds), so 10 ms ticks are more than sufficient. Lower priority than MeshRxTask ensures frame processing is never delayed by a maintenance cycle. Stack sized for `_sendRouteAdv()` (largest single call, ~1.8 KB).

### D6 — `loop()` removal vs. no-op

**Decision**: Remove `loop()` entirely (breaking change).

**Rationale**: A no-op stub would silently hide the API contract change from callers and prevent future use of `loop()` for application-level hooks. Removing it forces callers to acknowledge the threading model change at compile time. All in-tree examples are updated as part of this change.

### D7 — `shutdown()` sequencing

**Decision**: Delete tasks before destroying the mutex; use `vTaskDelete(NULL)` pattern from within the tasks on a shutdown flag, not `vTaskDelete(handle)` from outside.

**Rationale**: Deleting a task from outside while it holds the mutex leaves the mutex in an undefined state. Setting a `_shutdown` flag and letting tasks self-terminate after releasing the mutex is safe. A `_shutdownSem` (binary semaphore) allows `shutdown()` to block until both tasks have exited before destroying shared resources.

## Risks / Trade-offs

| Risk | Mitigation |
|---|---|
| MeshRxTask stack overflow during ECDH on deep call stacks | Set `CONFIG_FREERTOS_USE_TRACE_FACILITY` + `uxTaskGetStackHighWaterMark()` log in debug builds |
| RxQueue full — frames dropped under burst traffic | Log a warning counter; queue depth can be tuned via `meshConfig.h` constant |
| Mutex contention stalling the WiFi task enqueue path | `_onFrameReceived` never acquires the mutex — it only calls `xQueueSendFromISR` which is lock-free |
| `shutdown()` called before tasks start (e.g., `begin()` fails mid-way) | Guard: only delete task handles that are non-null |
| Arduino `loop()` removal breaks user sketches | Documented as breaking change; in-tree examples updated; clear compile error guides users |

## Migration Plan

1. Update `MeshNetwork.h` — add handles, queue, mutex; remove `loop()` declaration.
2. Refactor `MeshNetwork.cpp` — implement task entry functions, refactor `begin()` and `shutdown()`.
3. Update all in-tree examples — remove `mesh.loop()` calls.
4. Update `test/` mocks if any mock references `loop()`.
5. Build and run existing PlatformIO test suite to verify no regressions.
6. Flash `GatewaySingleChip` + `NodePing` pair; verify beacon exchange, key handshake, and ping round-trip operate correctly.

## Open Questions

- Should `MeshRxTask` and `MeshMaintenanceTask` stack sizes be compile-time configurable via `meshConfig.h`, or are the hardcoded defaults sufficient for v1?
- Is a watchdog reset guard needed if `_handleFrame()` takes >1 s (e.g., during ECDH on a heavily loaded system)?
