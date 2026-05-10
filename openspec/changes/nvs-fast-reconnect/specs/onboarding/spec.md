## MODIFIED Requirements

### Requirement: Node boot sequence includes fast-boot path
The onboarding sequence SHALL begin with a fast-boot check before any channel scan or AP provisioning attempt. The fast-boot path is applicable only to nodes in `MESH_NODE` or `MESH_BATTERY` mode.

**Fast-boot procedure:**
1. Read NVS cache from `enigma_cc` namespace.
2. Validate schema version, channel, NetworkID, and gateway MAC.
3. If valid: configure ESP-NOW channel from cache, restore `PeerManager` from cached peers, skip to connected state.
4. If invalid or absent: proceed with existing onboarding (AP provisioning → blind channel scan → JOIN_BEACON).
5. If fast-boot succeeds but no peer ACKs within `FAST_BOOT_TIMEOUT_MS` (4 000 ms): invalidate cache, fall back to existing onboarding.

#### Scenario: Node with valid cache skips channel scan
- **WHEN** a node in `MESH_NODE` mode boots with a valid `enigma_cc` NVS cache
- **THEN** the system SHALL configure the ESP-NOW channel from the cache without performing a WiFi scan or connecting to the provisioning AP

#### Scenario: Node with valid cache skips JOIN_BEACON exchange
- **WHEN** a node restores connection state from cache and cached peers are reachable
- **THEN** the system SHALL send the first data frame directly without sending or waiting for a JOIN_BEACON

#### Scenario: Node with stale cache falls back to normal onboarding
- **WHEN** a node restores state from cache but no cached peer responds within `FAST_BOOT_TIMEOUT_MS`
- **THEN** the system SHALL invalidate the NVS cache and proceed with full onboarding (channel scan, JOIN_BEACON)

#### Scenario: Gateway always runs full onboarding
- **WHEN** a node in `MESH_GATEWAY` mode boots
- **THEN** the system SHALL ignore any `enigma_cc` NVS cache and execute full onboarding to refresh AP provisioning data

#### Scenario: Node with absent or corrupt cache runs full onboarding
- **WHEN** NVS returns `ESP_ERR_NVS_NOT_FOUND` or `ESP_ERR_NVS_INVALID_CRC` for any `enigma_cc` key
- **THEN** the system SHALL execute the existing onboarding sequence unchanged
