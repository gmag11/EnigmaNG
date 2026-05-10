## ADDED Requirements

### Requirement: Gateway as IPv6 router for the mesh prefix
The gateway SHALL act as an IPv6 router for the mesh ULA prefix. It SHALL emit periodic ICMPv6 Router Advertisements (RA) on the `mesh0` interface with the configured prefix (`ENIGMANG_IPV6_ULA_PREFIX`) so that nodes perform SLAAC. Additionally, it SHALL emit RAs on the WiFi STA interface (`wifi_sta`) advertising the mesh prefix, so that the user's WiFi router can configure a static route to said prefix.

#### Scenario: Periodic RA towards the mesh (node SLAAC)
- **WHEN** the gateway has IPv6 enabled and is connected to the mesh
- **THEN** it emits ICMPv6 RA on `mesh0` every `IPV6_RA_INTERVAL` seconds (default 60s) with the ULA prefix and `A-flag=1` (SLAAC)
- **THEN** the mesh nodes configure their routable address via SLAAC without manual intervention

#### Scenario: Periodic RA towards the WiFi LAN (mesh prefix advertisement)
- **WHEN** the gateway is connected to the WiFi AP and has IPv6 enabled
- **THEN** it emits ICMPv6 RA on `wifi_sta` with the mesh prefix and `A-flag=0` (route-only, no SLAAC for the LAN)
- **THEN** the user's WiFi router can see the mesh prefix and configure a static route

#### Scenario: IPv6 LAN → mesh node routing
- **WHEN** a host on the WiFi LAN sends an IPv6 packet to an address in the mesh prefix (`fd00:mesh::/48`)
- **THEN** the gateway receives it on `wifi_sta`, routes it to `mesh0` using the `RouteEntry6` table
- **THEN** the destination mesh node receives the packet (bidirectional connectivity, no NAT)

### Requirement: No IPv6 NAT on the gateway
The gateway SHALL NOT apply NAPT/masquerade to IPv6 traffic. All IPv6 traffic between the mesh and the LAN/Internet SHALL use direct routing with the nodes' ULA or global addresses. The `ip_napt_enable()` function SHALL NOT be activated for the IPv6 netif.

#### Scenario: Mesh → LAN IPv6 traffic without NAT
- **WHEN** a mesh node sends an IPv6 packet to a host on the LAN
- **THEN** the packet reaches the host with the node's ULA IPv6 address as source (no translation)
- **THEN** the host can respond directly to the node's IPv6 address

### Requirement: IPv6 prefix configuration in the gateway API
`Gateway::begin()` SHALL accept an optional ULA IPv6 prefix parameter. If not specified, it will use `ENIGMANG_IPV6_ULA_PREFIX` from `meshConfig.h`. The prefix SHALL be accepted in CIDR notation (`fd00:cafe::/48`).

#### Scenario: Gateway startup with custom IPv6 prefix
- **WHEN** `gateway.begin(ssid, pass, "fd12:3456::/48")` is called
- **THEN** the gateway uses `fd12:3456::/48` as the mesh prefix
- **THEN** the emitted RAs contain said prefix

#### Scenario: Gateway startup without IPv6 prefix (default)
- **WHEN** `gateway.begin(ssid, pass)` is called without an IPv6 prefix
- **THEN** the gateway uses `ENIGMANG_IPV6_ULA_PREFIX` defined in `meshConfig.h`

## MODIFIED Requirements

### Requirement: Routing: full NAT (masquerade)
The gateway applies full NAT masquerade for all **IPv4** outgoing traffic from the mesh towards the WiFi network or Internet. `ip_napt` is enabled on `mesh0` (lwIP NAPT, IDF 5.5.4). All IPv4 traffic from `mesh0` → `wifi_sta` is masqueraded with the gateway's WiFi IP. **NAT is not applied for IPv6** (see Requirement: No IPv6 NAT on the gateway).

#### Scenario: Mesh → Internet IPv4 traffic (NAT)
- **WHEN** a mesh node sends an IPv4 packet to the Internet
- **THEN** the gateway applies NAPT masquerading with its WiFi IP as source
- **THEN** the response reaches the gateway and is forwarded to the original node

#### Scenario: Mesh → LAN IPv6 traffic (no NAT)
- **WHEN** a mesh node sends an IPv6 packet to the LAN
- **THEN** the gateway routes it directly without modifying the IP addresses
- **THEN** the LAN host sees the node's ULA IPv6 address as source