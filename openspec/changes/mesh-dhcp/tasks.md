# Tasks: mesh-dhcp — DHCP Server on Gateway

## Progress: 0/3 tasks completed

---

## Phase 1: Mock and types

**Spec:** `openspec/specs/ip-netif/spec.md` §IP assignment

- [ ] Update `test/mocks/dhcpserver/dhcpserver.h` with real types and no-op stubs for the lwIP dhcpserver API
  - _Test: `pio test -e native` compiles without errors after the change_

---

## Phase 2: Implementation

**Spec:** `openspec/specs/ip-netif/spec.md` §IP assignment

- [ ] Implement `Gateway::startDHCPServer(poolStart, poolEnd)` using `dhcps_new()` / `dhcps_start()` on the `mesh0` netif; add `dhcps_handle_t _dhcpsHandle` in `Gateway.h`; invoke in `Gateway::begin()` and clean up in `Gateway::stop()`
  - _Test: `pio run -e esp32` compiles without warnings; on hardware: node without static IP obtains gateway DHCP IP in < 5s_

---

## Phase 3: Public API integration

- [ ] Verify that `GatewaySingleChip` and `gateway_hosted` examples start the DHCP server correctly; document behavior in comments of `MeshNetwork.h`
  - _Test: both examples compile; comment `// DHCP: nodes without static IP receive pool IP` visible in API_
