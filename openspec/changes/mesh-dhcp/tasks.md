# Tasks: mesh-dhcp — DHCP Server on Gateway

## Progress: 0/3 tasks completed

---

## Phase 1: Mocks and types

**Spec:** `openspec/specs/ip-netif/spec.md` §IP assignment

- [ ] Update `test/mocks/dhcpserver/dhcpserver.h` with concrete types and no-op stubs for the lwIP dhcpserver API
  - _Test: `pio test -e native` compiles without errors after the change_

---

## Phase 2: Implementation

**Spec:** `openspec/specs/ip-netif/spec.md` §IP assignment

- [ ] Implement `Gateway::startDHCPServer(poolStart, poolEnd)` using `dhcps_new()` / `dhcps_start()` on the `mesh0` netif; add `dhcps_handle_t _dhcpsHandle` in `Gateway.h`; call in `Gateway::begin()` and clean up in `Gateway::stop()`
  - _Test: `pio run -e esp32` compiles without warnings; on hardware a node without static IP obtains a DHCP IP from the gateway within 5s_

---

## Phase 3: Public API integration

- [ ] Verify that `GatewaySingleChip` and `gateway_hosted` examples start the DHCP server correctly; document behavior in `MeshNetwork.h` comments
  - _Test: both examples compile; comment `// DHCP: nodes without static IP receive pool IP` present in the API header_

---

## Phase 4: Tests — unit and integration

- [ ] 4.1 Unit test: `test/mocks/dhcpserver/dhcpserver.h` types and stubs compile under `pio test -e native`
  - _Check: `pio test -e native` returns success_
- [ ] 4.2 Unit test: Gateway DHCP start/stop compiles in `pio run -e esp32` and a unit test verifies `dhcps_start()` is invoked (mocked)
  - _Check: `pio run -e esp32` compiles without warnings; unit asserts `dhcps_start()` called_
- [ ] 4.3 Integration test: `GatewaySingleChip` boots and a client node obtains an IP from the DHCP pool within 5s (hardware or simulated)
  - _Check: on hardware or simulation, DHCP client receives IP from pool within 5s_

