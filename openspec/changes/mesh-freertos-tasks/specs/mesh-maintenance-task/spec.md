## ADDED Requirements

### Requirement: MeshMaintenanceTask drives periodic mesh housekeeping
The mesh stack SHALL run a dedicated FreeRTOS task (`MeshMaintenanceTask`) that wakes every 10 ms and executes all time-driven mesh operations: JOIN_BEACON transmission, ROUTE_ADV transmission, peer timeout checking, epoch rotation, and gratuitous ARP. The application SHALL NOT need to call any method to trigger these operations.

#### Scenario: Beacon sent on schedule
- **WHEN** the beacon interval elapses (as tracked internally by `_lastBeaconMs`)
- **THEN** `MeshMaintenanceTask` SHALL call `_sendJoinBeacon()` without any application involvement

#### Scenario: Peer timeout enforced while application blocks
- **WHEN** the application task is blocked for several seconds (e.g., reconnecting to an MQTT broker)
- **THEN** `MeshMaintenanceTask` SHALL still call `_checkPeerTimeouts()` and evict stale peers on schedule

### Requirement: MeshMaintenanceTask holds mutex during state access
MeshMaintenanceTask SHALL acquire the shared recursive mutex before accessing PeerManager, Router, or any other shared mesh state, and SHALL release it after each maintenance cycle completes.

#### Scenario: Maintenance cycle completes without starving MeshRxTask
- **WHEN** MeshMaintenanceTask acquires the mutex to run `_sendRouteAdv()`
- **THEN** the mutex SHALL be released before the task delays until the next tick, allowing MeshRxTask to acquire it for incoming frame processing

### Requirement: MeshMaintenanceTask terminates cleanly on shutdown
When `MeshNetwork::shutdown()` is called, `MeshMaintenanceTask` SHALL detect the shutdown signal, release any held mutex, and self-terminate via `vTaskDelete(NULL)`.

#### Scenario: Shutdown while maintenance cycle is running
- **WHEN** `shutdown()` sets the shutdown flag while `MeshMaintenanceTask` holds the mutex
- **THEN** the task SHALL finish the current cycle, release the mutex, and then self-terminate
