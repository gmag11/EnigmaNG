# Proposal: DHCP Server on Gateway (mesh-dhcp)

## What will be built?

Implement the DHCP server on the EnigmaNG gateway so mesh nodes can obtain a dynamic IP address without static NVS assignment.

## Why?

The `ip-netif` specification defines three IP assignment modes for nodes:
- **Distributed static** (already implemented): MAC→IP table in NVS, distributed via ROUTE_ADV.
- **DHCP** (pending): server on the gateway using lwIP `dhcpserver`.
- **Manual static IP** (already implemented): `begin(psk, IPAddress(...))`.

Without the DHCP server, plug-and-play nodes without preassigned IP cannot automatically join the mesh. The function `Gateway::startDHCPServer()` exists in code but is an empty stub.

## Scope

### Includes

- Real implementation of `Gateway::startDHCPServer(poolStart, poolEnd)` using the lwIP `dhcpserver` API (available in ESP-IDF 5.x via Arduino Core ESP32 3.3.8).
- Integration in `Gateway::begin()` to automatically enable the server on the `mesh0` interface.
- Default address pool configuration: `10.200.0.2` – `10.200.0.254` (subnet `10.200.0.0/16`).
- Update the stub in `test/mocks/dhcpserver/dhcpserver.h` to support the real types and functions.
- Unit test verifying the server starts and serves an IP to a node without static IP.

### Excludes

- DHCP relay (not needed: all mesh nodes see the gateway directly at L3).
- Option 43/60 or extended DHCP options.
- ESP8266 support (no full IP stack; uses MQTT Proxy).
