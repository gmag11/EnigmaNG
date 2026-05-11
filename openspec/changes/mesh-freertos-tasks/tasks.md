## 1. Header Changes

- [ ] 1.1 Add `#include "freertos/FreeRTOS.h"`, `freertos/task.h`, `freertos/queue.h`, `freertos/semphr.h` to `MeshNetwork.h` (guarded by `#if !defined(ESP8266)`). _Test: file compiles without errors on ESP32 target._
- [ ] 1.2 Add private members to `MeshNetwork`: `QueueHandle_t _rxQueue`, `TaskHandle_t _rxTask`, `TaskHandle_t _maintTask`, `SemaphoreHandle_t _mutex`, `volatile bool _shutdown`. _Test: no size regression on ESP8266 build._
- [ ] 1.3 Remove `void loop()` declaration from `MeshNetwork.h`. _Test: compiler error if any caller references `loop()`._
- [ ] 1.4 Add private static task entry declarations: `static void _rxTaskEntry(void* arg)` and `static void _maintTaskEntry(void* arg)`. _Test: functions declared and callable as FreeRTOS task entries._

## 2. RxQueue and Frame Enqueuing

- [ ] 2.1 Define `RxQueueItem` struct in `MeshNetwork.h`: `uint8_t srcMac[6]`, `uint8_t frame[250]`, `uint8_t len`, `int8_t rssi`. Verify total size ≤ 258 bytes. _Test: `sizeof(RxQueueItem) <= 258`._
- [ ] 2.2 In `begin()`, create the queue: `_rxQueue = xQueueCreate(8, sizeof(RxQueueItem))`. Return `false` if creation fails. _Test: `begin()` returns false when heap is insufficient._
- [ ] 2.3 Refactor `_onFrameReceived()` to only copy the frame into an `RxQueueItem` and call `xQueueSendFromISR`. Remove all parsing/processing logic from this function. _Test: no call to `_handleFrame()` inside `_onFrameReceived`._
- [ ] 2.4 Verify drop counter is incremented when `xQueueSendFromISR` returns `errQUEUE_FULL`. _Test: unit test with queue depth 0 confirms counter increments._

## 3. MeshRxTask

- [ ] 3.1 Implement `_rxTaskEntry(void* arg)`: infinite loop that blocks on `xQueueReceive(_rxQueue, &item, portMAX_DELAY)`, takes the recursive mutex, calls `_handleFrame()`, then releases the mutex. Exits loop when `_shutdown` is true. _Test: task processes exactly one item per queue entry._
- [ ] 3.2 In `begin()`, create the task via `xTaskCreate(_rxTaskEntry, "MeshRx", 6144, this, tskIDLE_PRIORITY + 5, &_rxTask)`. Return `false` if creation fails. _Test: `begin()` returns false on task creation failure._
- [ ] 3.3 Verify `_handleFrame()` and all its callees (`_handleJoinBeacon`, `_handleKeyExchHello`, etc.) do NOT call `xQueueSendFromISR` or any ISR-safe API (no re-entrant enqueue). _Test: code review + static analysis._

## 4. MeshMaintenanceTask

- [ ] 4.1 Implement `_maintTaskEntry(void* arg)`: loop with `vTaskDelay(pdMS_TO_TICKS(10))`; on each tick, if `_shutdown` is false, take mutex, call `_sendJoinBeacon()`, `_sendRouteAdv()`, `_checkPeerTimeouts()`, `_rotateEpoch()`, `_sendGratuitousArp()`, release mutex. Exits when `_shutdown` is true. _Test: beacon transmission occurs without `loop()` being called._
- [ ] 4.2 In `begin()`, create the task via `xTaskCreate(_maintTaskEntry, "MeshMaint", 3072, this, tskIDLE_PRIORITY + 3, &_maintTask)`. Return `false` if creation fails. _Test: `begin()` returns false on task creation failure._
- [ ] 4.3 Verify all millis()-based interval checks inside `_sendJoinBeacon()`, `_sendRouteAdv()` etc. remain unchanged — timing logic stays in the helper functions. _Test: beacon is sent at the configured interval (±50 ms)._

## 5. Mutex Initialization and Protection

- [ ] 5.1 In `begin()`, create mutex: `_mutex = xSemaphoreCreateRecursiveMutex()`. Return `false` if creation fails. _Test: `begin()` returns false when mutex creation fails._
- [ ] 5.2 Audit all methods that access PeerManager, Router, Fragmentation, HandshakeContext, or NackBuffer; confirm they are only called while the mutex is held (within task entry functions). _Test: no direct public call path to shared state without mutex._
- [ ] 5.3 Verify `_onFrameReceived()` does NOT acquire the mutex (it must be ISR-safe and lock-free). _Test: no `xSemaphoreTake` call inside `_onFrameReceived`._

## 6. Shutdown Sequencing

- [ ] 6.1 Implement `shutdown()`: set `_shutdown = true`; wait for both tasks to delete themselves using a binary semaphore signal (`_shutdownSem`); then delete the queue and mutex. _Test: `shutdown()` returns without deadlock when called after `begin()`._
- [ ] 6.2 Both task entry functions SHALL signal `_shutdownSem` via `xSemaphoreGive` just before calling `vTaskDelete(NULL)`. _Test: `shutdown()` unblocks within 50 ms of being called._
- [ ] 6.3 Guard all handle deletions: only delete non-null handles. _Test: calling `shutdown()` before `begin()` does not crash._

## 7. Remove `loop()` Implementation

- [ ] 7.1 Delete the `loop()` method body from `MeshNetwork.cpp`. _Test: linker error if any translation unit references `MeshNetwork::loop`._
- [ ] 7.2 Remove `loop()` calls from all in-tree Arduino examples: `BasicNode`, `BatteryNode`, `GatewaySingleChip`, `GatewayPing`, `IperfNode`, `MeshNode8266`, `MqttNode`, `NodePing`. _Test: all examples build cleanly._
- [ ] 7.3 Remove `mesh.loop()` from the IDF `gateway_hosted` example `main.cpp` main loop. _Test: `gateway_hosted` builds cleanly._

## 8. Test Suite Updates

- [ ] 8.1 Update or remove any test mock/stub that references `MeshNetwork::loop()`. _Test: `pio test` passes with no references to `loop()`._
- [ ] 8.2 Add a unit test that verifies `_onFrameReceived` only enqueues and does not call `_handleFrame` directly. _Test: mock confirms `_handleFrame` not called from WiFi task context._
- [ ] 8.3 Add a unit test that verifies beacon transmission occurs after `begin()` without any `loop()` call. _Test: mock confirms `_sendJoinBeacon` is called within 2× the beacon interval._

## 9. Integration Verification

- [ ] 9.1 Build all PlatformIO environments (`pio run`) and confirm zero errors. _Test: CI build passes._
- [ ] 9.2 Flash `GatewaySingleChip` + `NodePing` pair; verify JOIN_BEACON exchange, ECDH handshake, and ping round-trip succeed. _Test: serial monitor shows `[Mesh] keyEstablished` and ICMP reply._
- [ ] 9.3 Confirm no watchdog resets during sustained iperf traffic (`IperfNode`). _Test: 60-second iperf run with no WDT reset in serial log._
- [ ] 9.4 Measure task stack high-water marks in a debug build; confirm `MeshRxTask` headroom ≥ 512 bytes and `MeshMaintenanceTask` headroom ≥ 512 bytes. _Test: `uxTaskGetStackHighWaterMark()` log entries._
