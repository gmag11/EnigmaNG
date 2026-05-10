## ADDED Requirements

### Requirement: MTU and IPv6 headers on mesh0
The virtual `mesh0` interface operates with MTU=216B for both IPv4 and IPv6. The IPv6 header is 40B (vs 20B IPv4), reducing the available payloads:

```
TCP MSS IPv6:       156 bytes  (216 - 40 IPv6 - 20 TCP)
UDP max payload:    168 bytes  (216 - 40 IPv6 - 8 UDP)
```

`lwipopts.h` SHALL enable:
```c
#define LWIP_IPV6          1
#define LWIP_IPV6_FRAG     1
#define LWIP_IPV6_REASS    1
#define LWIP_IPV6_AUTOCONFIG 1
```

The 216B MTU is below the IPv6 minimum of 1280B (RFC 8200). This limitation SHALL be explicitly documented. For typical EnigmaNG use cases (MQTT, CoAP, sensor data), payloads are ≤168B, so the limitation is acceptable in practice.

#### Scenario: IPv6 ping within the mesh
- **WHEN** a node sends an ICMPv6 echo request to another mesh node
- **THEN** the total payload (40B IPv6 + 8B ICMPv6 + data) fits in a single frame if data ≤168B
- **THEN** the destination node responds correctly with ICMPv6 echo reply

#### Scenario: MTU limitation documentation
- **WHEN** a user attempts to send an IPv6 packet with payload &gt; 168B (UDP) or &gt; 156B (TCP)
- **THEN** lwIP fragments using the EnigmaNG L2 fragmentation layer (`DATA_FRAG`)
- **THEN** the receiver reassembles the packet (timeout 2s, max 4 fragments)

### Requirement: IPv6 address assignment on mesh0
In addition to the existing IPv4 assignment scheme, each node SHALL obtain:

1. **Link-local:** `fe80::&lt;EUI-64&gt;/10` derived from the MAC, automatically assigned by lwIP when IPv6 is enabled on `mesh0`.
2. **Routable ULA:** `ENIGMANG_IPV6_ULA_PREFIX + EUI-64(MAC)` — statically assigned from `meshConfig.h` or via SLAAC if the gateway emits an RA with the prefix.

IPv6 assignment is complementary to IPv4; both coexist on `mesh0`.

#### Scenario: IPv4 and IPv6 coexistence on mesh0
- **WHEN** the node has both IPv4 and IPv6 enabled
- **THEN** `mesh0` simultaneously has an IPv4 address (`10.200.x.x`) and an IPv6 link-local + a ULA address
- **THEN** applications can use either IPv4 or IPv6 sockets indistinctly

#### Scenario: Route resolution for IPv6 packets
- **WHEN** lwIP needs to send an IPv6 packet over `mesh0`
- **THEN** `mesh_netif_output()` queries the `RouteEntry6` table to obtain the `nextHop` MAC
- **THEN** if there is no route in `RouteEntry6`, the packet is dropped (ARP and the IPv4 table are not used)