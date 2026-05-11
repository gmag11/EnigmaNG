## MODIFIED Requirements

### Requirement: begin() initializes the mesh and starts background tasks
`begin()` SHALL initialize all mesh subsystems (WiFi mode, ESP-NOW physical layer, mesh0 netif, onboarding AP) and SHALL create and start the `MeshRxTask` and `MeshMaintenanceTask` FreeRTOS tasks before returning. `begin()` SHALL return `true` only if all subsystems and both tasks are successfully initialized.

#### Scenario: Successful initialization
- **WHEN** `begin("MyPSK", MESH_NODE)` is called on a freshly powered ESP32
- **THEN** both FreeRTOS tasks SHALL be running, the mesh0 netif SHALL be up, and `begin()` SHALL return `true`

#### Scenario: Task creation failure
- **WHEN** `xTaskCreate` fails for either task (e.g., insufficient heap)
- **THEN** `begin()` SHALL return `false` and any already-started resources SHALL be cleaned up

## REMOVED Requirements

### Requirement: loop() drives mesh maintenance
**Reason**: Replaced by `MeshMaintenanceTask` (FreeRTOS task). Periodic mesh operations (beacons, route advertisements, peer timeouts) are now driven by the task independently of the application.
**Migration**: Remove all `mesh.loop()` calls from application code. No replacement call is needed — the mesh runs autonomously after `begin()`.
