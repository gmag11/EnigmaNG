# Spec: Mesh Time Synchronization

**Capability:** `time-sync`
**Change:** `mesh-time-sync`

## Purpose

Define the protocol for distributing accurate wall-clock time from the gateway (NTP source) to mesh nodes, enabling standard POSIX/IDF time functions to return real timestamps on every node.

## Frame definitions

Two new frame types are added to `FrameType` in `LinkLayer.h`:

```
TIME_REQ  = 0x10   // Node → Gateway peer: request current time
TIME_SYNC = 0x11   // Gateway → Node peer: deliver current time (unicast or unsolicited)
```

Both use `Protocol::MESH_INTERNAL` and are encrypted.

**TIME_REQ payload (4 bytes):**
```c
struct TimeSyncReq {
    uint32_t tx_ms;   // sender's millis() at transmission time (RTT anchor)
};
```

**TIME_SYNC payload (12 bytes):**
```c
struct TimeSyncResp {
    uint32_t unix_sec;    // UTC seconds since Unix epoch
    uint16_t unix_ms;     // sub-second milliseconds part
    uint32_t req_tx_ms;   // echo of TimeSyncReq.tx_ms (0 if unsolicited broadcast)
    uint16_t gw_proc_ms;  // gateway processing delay in ms
};
```

## RTT compensation

When a node receives a `TIME_SYNC` that is a response to its own `TIME_REQ` (`req_tx_ms != 0`):

```
rtt_ms       = millis() - req_tx_ms
flight_ms    = (rtt_ms - gw_proc_ms) / 2
corrected_ms = unix_sec * 1000ULL + unix_ms + flight_ms
```

The node applies `settimeofday()` for the first sync or when the offset exceeds 2 s; otherwise `adjtime()` for gradual adjustment.

For unsolicited `TIME_SYNC` broadcasts (`req_tx_ms = 0`), no RTT correction is applied.

## ADDED Requirements

### Requirement: TIME_REQ frame sent on demand
A node with an established session with the gateway peer SHALL send a `TIME_REQ` frame to that peer when `syncTime()` is called. `syncTime()` SHALL return `false` if no gateway peer has a completed key exchange session, and `true` otherwise.

#### Scenario: Successful TIME_REQ
- **WHEN** `mesh.syncTime()` is called and a completed handshake with the gateway peer exists
- **THEN** a `TIME_REQ` frame is sent unicast to the gateway peer and `syncTime()` returns `true`

#### Scenario: No gateway peer available
- **WHEN** `mesh.syncTime()` is called but no gateway peer with a completed session exists
- **THEN** `syncTime()` returns `false` and no frame is sent

### Requirement: Gateway responds to TIME_REQ with TIME_SYNC
On receiving a `TIME_REQ` frame, the gateway SHALL reply with a unicast `TIME_SYNC` frame containing the current UTC time (from the NTP-synchronized system clock), the echoed `req_tx_ms`, and the measured processing delay `gw_proc_ms`.

The gateway SHALL NOT respond if `isTimeSynced()` returns `false` on the gateway.

#### Scenario: Gateway synced, node requests time
- **WHEN** the gateway receives a `TIME_REQ` and its system clock has been set via NTP
- **THEN** the gateway sends a unicast `TIME_SYNC` to the requesting peer within 50 ms

#### Scenario: Gateway not yet NTP-synced
- **WHEN** the gateway receives a `TIME_REQ` but `isTimeSynced()` is `false`
- **THEN** no `TIME_SYNC` is sent; the request is silently dropped

### Requirement: Node sets system clock on TIME_SYNC receipt
On receiving a valid `TIME_SYNC`, the node SHALL:
1. Apply RTT compensation if `req_tx_ms != 0`.
2. Call `settimeofday()` if this is the first sync or the offset exceeds 2 000 ms.
3. Call `adjtime()` for smaller corrections.
4. Set the internal `_timeSynced = true` flag.
5. Fire the `onTimeSync` callback with the corrected `time_t`.

#### Scenario: First time sync received
- **WHEN** a node receives its first valid `TIME_SYNC` with a sane epoch (> 1 000 000 000)
- **THEN** `settimeofday()` is called, `isTimeSynced()` returns `true`, and `onTimeSync` callback fires

#### Scenario: Subsequent small correction
- **WHEN** a node receives a `TIME_SYNC` and the offset is less than 2 000 ms
- **THEN** `adjtime()` is used instead of `settimeofday()`, avoiding time jumps

### Requirement: Gateway broadcasts TIME_SYNC on NTP sync and periodically
After acquiring NTP time for the first time, the gateway SHALL send a unicast `TIME_SYNC` (with `req_tx_ms = 0`) to each peer with a completed key exchange session. The gateway SHALL repeat this broadcast every `MESH_TIME_BROADCAST_INTERVAL_MS` (default 600 000 ms).

#### Scenario: NTP first sync
- **WHEN** the gateway's SNTP callback fires for the first time
- **THEN** a `TIME_SYNC` is sent to every established peer within 1 s

#### Scenario: Periodic broadcast
- **WHEN** `MESH_TIME_BROADCAST_INTERVAL_MS` has elapsed since the last broadcast
- **THEN** the gateway sends unicast `TIME_SYNC` to all established peers

### Requirement: TIME_SYNC sent to new peers after handshake
The gateway SHALL send a unicast `TIME_SYNC` to a node immediately after its key exchange handshake completes, provided the gateway's clock is already synced.

#### Scenario: Node joins with synced gateway
- **WHEN** a node completes the handshake with the gateway and the gateway `isTimeSynced()` is `true`
- **THEN** the gateway sends a unicast `TIME_SYNC` to that node within 200 ms of handshake completion

### Requirement: `isTimeSynced()` reflects clock state
`isTimeSynced()` SHALL return `true` on any node (including the gateway) once its system clock has been set to a valid UTC time (epoch > 1 000 000 000) at least once since boot.

#### Scenario: Before first sync
- **WHEN** `isTimeSynced()` is called before any time sync has occurred
- **THEN** it returns `false`

#### Scenario: After successful sync
- **WHEN** `settimeofday()` has been called with a valid epoch
- **THEN** `isTimeSynced()` returns `true`
