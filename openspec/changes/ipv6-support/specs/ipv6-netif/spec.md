# Spec: IPv6 in the Mesh — netif, addressing, NDP, routing table

## ADDED Requirements

### Requirement: Automatic link-local address on mesh0
Each ESP32 node with IPv6 enabled SHALL automatically configure an IPv6 link-local address (`fe80::/10`) on `mesh0` derived from the EUI-64 of its MAC. The Universal/Local bit (bit 7 of the first octet of the IID) SHALL be inverted per RFC 4291 §2.5.1. The address SHALL be operational before the node joins the mesh.

#### Scenario: Link-local assignment on startup
- **WHEN** the node calls `MeshNetwork::begin()` with IPv6 enabled (`ENIGMANG_IPV6_ENABLED`)
- **THEN** `esp_netif_create_ip6_linklocal()` assigns `fe80::&lt;EUI-64&gt;/10` on `mesh0`
- **THEN** the link-local address is accessible via `esp_netif_get_ip6_linklocal()`

#### Scenario: Uniqueness of the link-local address
- **WHEN** two different nodes have different MACs
- **THEN** their link-local addresses `fe80::&lt;EUI-64&gt;` are different (no collision)

### Requirement: Routable ULA IPv6 address
Each node SHALL obtain a routable IPv6 address within the configured ULA prefix (`ENIGMANG_IPV6_ULA_PREFIX`, default `fd00::/48`). The host-part SHALL be derived from the node's MAC EUI-64. This address SHALL be assigned after receiving a Router Advertisement from the gateway with the mesh prefix, or, in the absence of a gateway, using the static ULA prefix from `meshConfig.h`.

#### Scenario: Static ULA address assignment
- **WHEN** `ENIGMANG_IPV6_ENABLED` is active and there is no active gateway
- **THEN** the node constructs `ENIGMANG_IPV6_ULA_PREFIX + EUI-64(MAC)` as its routable address
- **THEN** the address is registered on `mesh0` via `esp_netif_set_ip6_addr()`

#### Scenario: SLAAC assignment from gateway RA
- **WHEN** the node receives an ICMPv6 Router Advertisement from the gateway with the mesh prefix
- **THEN** lwIP constructs the address `&lt;RA-prefix&gt;/&lt;len&gt; + EUI-64(MAC)` via SLAAC (RFC 4862)
- **THEN** the routable address is configured on `mesh0`

### Requirement: Independent IPv6 routing table
The Router SHALL maintain a `RouteEntry6` table separate from the IPv4 table, with a static pool of `MAX_ROUTES6` entries (default 32, configurable in `meshConfig.h`). The structure SHALL be:

```cpp
struct RouteEntry6 {
    uint8_t  dst[16];     // destination IPv6 address
    uint8_t  prefixLen;   // prefix length (128 for hosts)
    uint8_t  mac[6];      // final destination MAC
    uint8_t  nextHop[6];  // next hop MAC
    uint8_t  hopCount;    // 0=local, 255=poison
    int8_t   rssi;
    uint32_t lastUpdate;  // millis()
    uint16_t ttl;         // seconds
    uint8_t  flags;       // IS_GATEWAY | IS_BATTERY | IS_DIRECT
};
// sizeof(RouteEntry6) = 43 bytes
// 32 entries = 1,376 bytes
```

#### Scenario: IPv6 route insertion
- **WHEN** the Router receives a `ROUTE_ADV` with `RA_FLAG_IPV6` flag with a valid IPv6 entry
- **THEN** the entry is inserted or updated in the `RouteEntry6` table
- **THEN** if the table is full, the eviction policy is applied: expired → highest hopCount → lowest lastUpdate

#### Scenario: IPv6 route lookup for sending
- **WHEN** lwIP delivers an IPv6 packet to `mesh_netif_output()` with destination `dst6`
- **THEN** the Router searches `RouteEntry6` for the entry with the highest prefixLen matching `dst6`
- **THEN** the frame is sent to the `nextHop` of the found entry
- **WHEN** no route is found
- **THEN** the packet is dropped and lwIP receives `ERR_RTE`

### Requirement: IPv6 route advertisement (ROUTE_ADV with IPv6 flag)
Nodes SHALL include their IPv6 routes in `ROUTE_ADV` using the `RA_FLAG_IPV6 (0x02)` flag in the frame's flags field. The format of each IPv6 entry in the payload SHALL be:

```
[IPv6_dst: 16B][MAC_dest: 6B][HopCount: 1B][Flags: 1B] = 24 bytes/entry
```

Capacity per frame: 9 IPv6 entries (216B / 24B = 9). If the table has more than 9 IPv6 entries, the existing `RA_CONTINUATION` mechanism will be used.

#### Scenario: Sending ROUTE_ADV with IPv6 entries
- **WHEN** a node has entries in `RouteEntry6` and the `RA_INTERVAL` timer expires
- **THEN** it emits a `ROUTE_ADV` frame with `RA_FLAG_IPV6` including up to 9 IPv6 entries per frame
- **WHEN** there are more than 9 IPv6 entries
- **THEN** it emits additional frames with `RA_CONTINUATION` until the table is complete

#### Scenario: Receiving IPv6 ROUTE_ADV
- **WHEN** a node receives a `ROUTE_ADV` with `RA_FLAG_IPV6`
- **THEN** it processes each 24B entry updating its `RouteEntry6`
- **THEN** it applies Split Horizon: does not re-advertise routes whose `nextHop` is the sender of the RA

### Requirement: PROTO_IPV6 frame dispatch at the link layer
The link layer SHALL dispatch `DATA` frames with `Protocol = PROTO_IPV6 (0x02)` to the lwIP IPv6 stack via `esp_netif_receive()`, in the same way it does with `PROTO_IPV4 (0x01)`.

#### Scenario: Receiving an IPv6 frame
- **WHEN** `LinkLayer` receives a decrypted `DATA` frame with `Protocol = 0x02`
- **THEN** it calls `esp_netif_receive(mesh0_netif, payload, len, NULL)` delivering the packet to lwIP
- **THEN** lwIP processes the IPv6 packet (NDP, ICMPv6, TCP/UDP)

#### Scenario: Sending an IPv6 frame
- **WHEN** lwIP calls `mesh_netif_output()` with an IPv6 packet
- **THEN** `LinkLayer` creates a `DATA` frame with `Protocol = 0x02`
- **THEN** the frame is encrypted and sent to the `nextHop` according to the `RouteEntry6` table

### Requirement: Transparent NDP over the mesh
The NDP protocol (ICMPv6 type 133–137) SHALL travel over the mesh as normal `PROTO_IPV6` traffic. NDP multicast frames (destination `ff02::/16`) SHALL be sent as broadcast frames in the mesh (`DstMAC = FF:FF:FF:FF:FF:FF`). No specific NDP logic is required in the mesh layer: lwIP handles NDP internally.

#### Scenario: Router Solicitation from a node to the gateway
- **WHEN** a newly joined node emits an ICMPv6 Router Solicitation (`ff02::2`)
- **THEN** the frame travels as a `PROTO_IPV6` broadcast over the mesh
- **THEN** the gateway receives it via `mesh0` and lwIP responds with a Router Advertisement

### Requirement: IPv6 MTU and lwipopts configuration
The `mesh0` MTU for IPv6 SHALL remain at 216B. The IPv6 header is 40B, therefore:

```
TCP MSS IPv6:       156 bytes  (216 - 40 - 20)
UDP max payload:    168 bytes  (216 - 40 - 8)
```

`lwipopts.h` SHALL include `LWIP_IPV6 1`, `LWIP_IPV6_FRAG 1`, and `LWIP_IPV6_REASS 1`. It is documented that the 216B MTU is below the RFC 8200 minimum (1280B) and that large payloads may not work with strict PMTUD implementations.

#### Scenario: IPv6 ping between two mesh nodes
- **WHEN** a node sends `ping6` to the ULA address of another node
- **THEN** the ICMPv6 echo request packet (40B IPv6 + 8B ICMP + 32B data = 80B payload) fits in one frame (80B &lt; 216B)
- **THEN** the destination node responds and the RTT is comparable to IPv4 ping

#### Scenario: Fragmentation of a large IPv6 packet
- **WHEN** an application sends a UDP packet with 200B IPv6 payload (&gt; 168B maximum)
- **THEN** lwIP fragments the IPv6 packet using the existing L2 fragmentation
- **THEN** the receiver correctly reassembles it (timeout 2s, max 4 fragments)