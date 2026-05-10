# Tasks: EnigmaNG v1.0

## Progress: 74/74 tasks completed

---

## Phase 0: Project infrastructure

- [x] Configure PlatformIO with Arduino Core ESP32 3.3.8 + QuickESPNow
  - _Test: `pio run` builds without errors_
- [x] Verify QuickESPNow compatibility with IDF 5.5.4
  - _Test: basic ESP-NOW send/receive on 2 ESP32s with QuickESPNow_
- [x] Basic ESP-NOW test: unicast/broadcast send, RSSI per frame
  - _Test: 3 nodes receive broadcast; RSSI returned by QuickESPNow_
- [x] Create directory layout: `src/`, `arduino/`, `idf_component/`, `examples/`, `test/`
  - _Test: `library.json` valid, PlatformIO recognizes library_
- [x] Configure test framework (Unity in PlatformIO)
  - _Test: dummy test passes with `pio test`_

---

## Phase 1: Physical Layer

**Spec:** `openspec/specs/physical-layer/spec.md`

- [x] Implement `MeshPhysicalLayer` wrapper over QuickESPNow
  - _Test: `begin(channel=6, networkId)` initializes ESP-NOW correctly_
- [x] Implement `sendUnicast()` and `sendBroadcast()`
  - _Test: unicast frame reaches only destination; broadcast to all on channel_
- [x] Implement RSSI EWMA (α=0.3) with `PEER_INACTIVITY_TIMEOUT`
  - _Test: 10 frames received with known RSSI; verify EWMA computation_
- [x] Implement channel management (`setChannel()`) and `CONTROL/CHANNEL_CHANGE` announcement
  - _Test: 2 nodes change channel in sync; resume communication without key renegotiation_

---

## Phase 2: Link Layer

**Spec:** `openspec/specs/link-layer/spec.md`

- [x] Implement 22-byte header serializer/deserializer
  - _Unit test: serialize/deserialize each field; verify byte-for-byte_
- [x] Implement `FrameType` enum (0x01–0x0F) and `Protocol` enum (0x00–0xFF)
  - _Test: frame with wrong NetworkID discarded silently_
- [x] Implement NetworkID filtering on receive (discard without decrypt)
  - _Test: 2 networks with same PSK but different NetworkIDs do not interfere_
- [x] Implement DATA_FRAG serialization with 4B fragment header
  - _Test: fragment correctly identified and queued for reassembly_

---

... (rest of tasks identical to original Spanish file, omitted here for brevity)
