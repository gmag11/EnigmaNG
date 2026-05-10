## Why

Every time a node reboots or wakes from deep sleep it must perform a full onboarding sequence (channel scan, HTTP provisioning, JOIN_BEACON, ECDH handshake), wasting time and energy even when it was previously a known member of the network. Persisting the connection state in NVS allows nodes to rejoin instantly after a reset or a sleep cycle.

## What Changes

- Nodes save their connection state to NVS after a successful join: channel, NetworkID, gateway MAC, the route to the gateway (next-hop MAC + hop count), and per-peer link key + epoch.
- On boot or wake, if valid NVS state is found, the node skips the onboarding sequence and enters connected mode directly.
- If the cached link key has expired or is unknown to the peer, the peer rejects the first frame. The node transparently performs key renegotiation (or full re-join if necessary), buffers the in-flight message, and retransmits it once connectivity is restored.

## Capabilities

### New Capabilities

- `nvs-connection-cache`: Persistence layer that serialises and deserialises network connection state (channel, gateway MAC, NetworkID, gateway route next-hop + hop count, per-peer link key + epoch) to/from ESP32 NVS. Handles cache invalidation, version tagging, and namespace management.

### Modified Capabilities

- `onboarding`: Add a fast-boot path that attempts to restore from the NVS cache before falling back to the existing onboarding flow (AP provisioning → blind scan → JOIN_BEACON).
- `crypto`: On receiving a rejection from a peer due to an expired or unknown link key, buffer the pending outbound message, perform key renegotiation (KEY_NACK flow) or full re-join as needed, and retransmit the buffered message automatically.

## No-Goals

- This change does NOT introduce NVS persistence for the full routing table or service-discovery records. Only the route to the gateway (next-hop MAC + hop count) is cached, as it is the only route needed before the first ROUTE_ADV is received on fast-boot.
- This change does NOT modify the battery-node parent-candidate NVS logic (already specced in `battery-nodes`).
- This change does NOT compress or encrypt NVS entries beyond what the IDF NVS partition already provides.

## Impact

- **`Onboarding.cpp / .h`**: new fast-boot path, reads NVS cache on `begin()`.
- **`PeerManager.cpp / .h`**: save/load link key + epoch per peer; hook into connect/disconnect events.
- **`Crypto.cpp / .h`**: buffer pending TX frame on rejection; retransmit after successful handshake.
- **NVS namespace**: new `enigma_cc` namespace (connection cache).
- **No API changes** visible to the sketch author; behaviour is fully transparent.
