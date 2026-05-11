## ADDED Requirements

### Requirement: RxQueue buffers incoming frames from the WiFi task
The mesh stack SHALL maintain a fixed-depth FreeRTOS queue (`QueueHandle_t`) that holds raw received frames. The `_onFrameReceived` callback (invoked from the WiFi/ESP-NOW task) SHALL only copy the frame bytes, source MAC, RSSI, and length into the queue via `xQueueSendFromISR`, without performing any parsing, decryption, or routing work.

#### Scenario: Frame received while MeshRxTask is busy
- **WHEN** a frame arrives via ESP-NOW while the `MeshRxTask` is processing a previous frame
- **THEN** the frame SHALL be placed in the queue without blocking the WiFi task

#### Scenario: Queue full — frame dropped
- **WHEN** the receive queue is full (all 8 slots occupied)
- **THEN** the incoming frame SHALL be silently dropped and a drop counter SHALL be incremented

### Requirement: MeshRxTask processes frames outside the WiFi task
The mesh stack SHALL run a dedicated FreeRTOS task (`MeshRxTask`) that blocks on the receive queue and processes each dequeued frame: frame header parsing, AES-128-GCM decryption, handshake handling, routing updates, and netif packet injection. This task SHALL NOT run in the WiFi task context.

#### Scenario: Frame is fully processed by MeshRxTask
- **WHEN** a valid encrypted frame is dequeued from the RxQueue
- **THEN** the task SHALL decrypt, parse, and dispatch the frame to the appropriate handler (`_handleJoinBeacon`, `_handleData`, etc.) and release the mutex before blocking again

#### Scenario: Malformed frame is discarded
- **WHEN** a dequeued frame fails header validation or GCM authentication
- **THEN** the frame SHALL be discarded and the task SHALL return to blocking on the queue without crashing

### Requirement: MeshRxTask holds mutex during frame processing
MeshRxTask SHALL acquire the shared recursive mutex before accessing PeerManager, Router, Fragmentation, or HandshakeContext state, and SHALL release it after processing completes.

#### Scenario: Mutex acquired and released per frame
- **WHEN** MeshRxTask dequeues a frame
- **THEN** the mutex SHALL be taken before any shared-state access and released before the task blocks again on the queue
