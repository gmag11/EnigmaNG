# Spec Delta: Battery Nodes
# Change: mesh-time-sync

## ADDED Requirements

### Requirement: Battery node restores clock from RTC memory on wake
On waking from deep sleep, before attempting any network activity, the battery node SHALL call `settimeofday()` with the value stored in `BatteryState::lastMeshTime` if that value represents a sane epoch (> 1 000 000 000). This provides an approximate clock immediately on wake, bounding error to at most the deep-sleep interval.

#### Scenario: Valid time in RTC memory
- **WHEN** the node wakes from deep sleep and `BatteryState::lastMeshTime > 1 000 000 000`
- **THEN** `settimeofday()` is called with that value before the first frame is sent

#### Scenario: No valid time in RTC memory
- **WHEN** `BatteryState::lastMeshTime` is 0 or below the sanity threshold
- **THEN** no `settimeofday()` call is made; the node waits for a `TIME_SYNC` from its Parent

### Requirement: Battery node persists received time to RTC memory
After processing a valid `TIME_SYNC` (or completing `syncTime()`), the battery node SHALL call `BatteryNode::setMeshTime(corrected_t)` to store the corrected timestamp in `BatteryState::lastMeshTime` in RTC memory.

#### Scenario: TIME_SYNC received
- **WHEN** a battery node receives and processes a valid `TIME_SYNC` frame
- **THEN** `BatteryState::lastMeshTime` is updated with the corrected `time_t` before the node returns to deep sleep

### Requirement: Battery node requests time sync on wake when RTC time is stale
If the stored `BatteryState::lastMeshTime` is older than `MESH_TIME_STALE_THRESHOLD_S` (default 3600 s) relative to the current `millis()` drift estimate, the battery node SHALL call `syncTime()` during its active window to refresh the clock.

#### Scenario: Stale clock detected on wake
- **WHEN** the node wakes and the elapsed time since `lastMeshTime` exceeds `MESH_TIME_STALE_THRESHOLD_S`
- **THEN** `syncTime()` is called within the active window and the result is stored before sleep
