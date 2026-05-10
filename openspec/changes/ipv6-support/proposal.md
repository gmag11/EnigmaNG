## Why

EnigmaNG currently only supports IPv4 over the mesh. IPv6 is the de facto standard for modern IoT (Matter, Thread, ESP-IDF 5.x supports it natively via `esp_netif`), and users with IPv6-enabled home/industrial networks cannot integrate the nodes with the rest of the infrastructure without ad-hoc solutions. The addition of IPv6 is especially relevant because **the IPv4 NAT model disappears**: mesh nodes obtain globally routable addresses, enabling direct bidirectional connectivity from the LAN and from the Internet (if a global prefix is delegated).

## What Changes

- Activate `PROTO_IPV6 (0x02)` in the frame Protocol field (already reserved in the link layer).
- Add a second IPv6 address to `mesh0`: link-local (`fe80::/10`) derived from the MAC, and a routable address (ULA `fd00::/8` or global by delegation).
- Implement IPv6 routing table in the Router (parallel to IPv4, or unified).
- **No NAT on IPv6**: the gateway routes the mesh prefix to the WiFi network via automatic route propagation. Two mechanisms are supported so that **no manual configuration is needed on the user's router**:
  - **DHCPv6-PD (Prefix Delegation, RFC 3633):** the gateway requests a delegated prefix from the upstream router. The upstream router already knows the route because it delegated it itself — zero manual configuration. It is the preferred mechanism when the router supports it (OpenWrt, Fritz!Box, ISP with native IPv6, etc.).
  - **RA Route Information Option (RFC 4191):** the gateway includes a `Route Information` option in the ICMPv6 Router Advertisements it emits towards the LAN, advertising the mesh prefix. Routers that implement RFC 4191 install the route automatically upon receiving the RA. More limited compatibility (Linux, OpenWrt, some modern routers; not on most basic home routers).
  - **Fallback:** if no automatic mechanism is available, the user can add a static route on their router (`ip -6 route add fd00:mesh::/48 via &lt;gateway-WiFi-IP&gt;`).
- Neighbor Discovery (NDP) managed by lwIP; the mesh acts as a virtual layer 2 network.
- Prefix assignment: DHCPv6-PD (if the upstream AP supports it) or configurable static ULA prefix.

## Capabilities

### New Capabilities

- `ipv6-netif`: IPv6 support on the virtual `mesh0` interface: link-local address configuration, routable address assignment (static ULA or via RA/DHCPv6-PD), NDP neighbor table, integration with lwIP IPv6.

### Modified Capabilities

- `gateway`: The gateway acts as an IPv6 router for the mesh prefix. No IPv6 NAPT. Propagates the mesh prefix to upstream automatically via DHCPv6-PD and/or RA with Route Information Option (RFC 4191), eliminating the need for a static route on the user's router. IPv6 prefix configuration is added to the `Gateway::begin()` API.
- `ip-netif`: Extension of the IP assignment specification to cover IPv6: link-local address derived from the MAC EUI-64, routable address assigned from the prefix delegated to the gateway. The MTU for IPv6 is documented (no change in bytes, but the IP header is 40B instead of 20B).
- `link-layer`: Formal activation of `PROTO_IPV6 (0x02)` (was "Reserved for v1.x"). Typical IPv6 frame size documented.

## Impact

- **`src/Router.cpp/.h`**: extension of the routing table for IPv6 (entries with IPv6Address + prefix length).
- **`src/NetifDriver.cpp/.h`**: register IPv6 in `esp_netif`, add RA/NDP callback.
- **`src/Gateway.cpp/.h`**: IPv6 prefix advertisement logic (RA), ULA prefix configuration.
- **`src/LinkLayer.cpp/.h`**: dispatch of `PROTO_IPV6` frames to the lwIP IPv6 netif.
- **`src/meshConfig.h`**: IPv6 prefix constants (`ENIGMANG_IPV6_ULA_PREFIX`).
- **Public API**: `MeshNetwork::beginIPv6(prefix, prefixLen)` or extension of `begin()`.
- **No changes in**: crypto, onboarding, physical, battery, ESP8266 (no IP stack).
- **Out of scope**: stateful DHCPv6, IPv6-only (IPv4 remains mandatory), IPv6 support on ESP8266, IPv6 traffic filtering on the gateway, Matter/Thread integration, RIPng/OSPFv3/BGP as upstream route distribution protocol.