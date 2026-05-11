# Spec Delta: Library Public API
# Change: mesh-time-sync

## ADDED Requirements

### Requirement: `syncTime()` API
`MeshNetwork` SHALL expose a `syncTime()` method that initiates an on-demand time synchronization request to the gateway peer.

```cpp
bool syncTime();
```

Returns `true` if the `TIME_REQ` was sent, `false` if no gateway peer is available.

#### Scenario: syncTime with gateway available
- **WHEN** `mesh.syncTime()` is called on a node with an active session to the gateway
- **THEN** `TIME_REQ` is sent and `syncTime()` returns `true`

#### Scenario: syncTime without gateway
- **WHEN** `mesh.syncTime()` is called and no gateway session exists
- **THEN** `syncTime()` returns `false`

### Requirement: `isTimeSynced()` API
`MeshNetwork` SHALL expose `isTimeSynced()` to allow application code to check whether the system clock has been set.

```cpp
bool isTimeSynced();
```

#### Scenario: Check before sync
- **WHEN** `isTimeSynced()` is called before any successful time sync
- **THEN** it returns `false`

#### Scenario: Check after sync
- **WHEN** a valid `TIME_SYNC` has been processed
- **THEN** `isTimeSynced()` returns `true`

### Requirement: `enableNTP()` gateway API
`MeshNetwork` SHALL expose `enableNTP()` to configure NTP servers and timezone before or immediately after `begin()`. If not called, the gateway SHALL apply defaults (`pool.ntp.org`, `time.cloudflare.com`, `UTC0`).

```cpp
void enableNTP(const char* server1 = "pool.ntp.org",
               const char* server2 = "time.cloudflare.com",
               const char* tz      = "UTC0");
```

Calling `enableNTP()` on a non-gateway node SHALL be a no-op.

#### Scenario: Custom NTP server
- **WHEN** `enableNTP("ntp.example.com", "", "CET-1CEST,M3.5.0,M10.5.0/3")` is called before `begin()`
- **THEN** the gateway uses the specified server and timezone string

#### Scenario: enableNTP on node
- **WHEN** `enableNTP()` is called on a `MESH_NODE` mode device
- **THEN** no action is taken and no error is reported

## MODIFIED Requirements

### Requirement: Time synchronization callbacks
`MeshNetwork` SHALL expose `onTimeSync(cb)` where `cb` is called with the corrected `time_t` whenever the system clock is updated. The callback type SHALL be:

```cpp
typedef void (*MeshTimeCallback)(time_t correctedTime, bool fromNTP);
```

The `fromNTP` parameter is `true` if the update originated from SNTP (gateway only), `false` if from a received `TIME_SYNC` frame (node).

#### Scenario: Callback on node time sync
- **WHEN** a node successfully processes a `TIME_SYNC` frame
- **THEN** the `onTimeSync` callback fires with the corrected `time_t` and `fromNTP = false`

#### Scenario: Callback on gateway NTP sync
- **WHEN** the gateway's SNTP callback fires
- **THEN** the `onTimeSync` callback fires with the current `time_t` and `fromNTP = true`
