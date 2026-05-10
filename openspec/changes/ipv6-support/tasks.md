## 1. Configuration and feature flag

- [ ] 1.1 Add `#define ENIGMANG_IPV6_ENABLED` in `meshConfig.h` (disabled by default). Verify: compiles without behavior changes when disabled.
- [ ] 1.2 Add `ENIGMANG_IPV6_ULA_PREFIX` in `meshConfig.h` (default `"fd00::/48"`). Verify: the constant is accessible from all modules.
- [ ] 1.3 Update `lwipopts.h` with `LWIP_IPV6 1`, `LWIP_IPV6_FRAG 1`, `LWIP_IPV6_REASS 1`, `LWIP_IPV6_AUTOCONFIG 1` under `#ifdef ENIGMANG_IPV6_ENABLED`. Verify: project compiles with IDF 5.5.4.

## 2. Link layer — PROTO_IPV6 activation

- [ ] 2.1 In `LinkLayer.h`, add the constant `PROTO_IPV6 = 0x02` to the protocol enum (already reserved; formalize it). Verify: the value does not collide with others.
- [ ] 2.2 In `LinkLayer.cpp`, add `case PROTO_IPV6:` branch in the received `DATA` frame dispatcher: call `esp_netif_receive()` just like `PROTO_IPV4`. Under `#ifdef ENIGMANG_IPV6_ENABLED`; outside the ifdef, silent discard. Verify: test `PROTO_IPV6` frame reaches lwIP.
- [ ] 2.3 In `mesh_netif_output()` (or equivalent in `NetifDriver.cpp`), add logic to detect if the packet is IPv6 (first nibble of payload = `0x60`) and emit the frame with `Protocol = PROTO_IPV6`. Verify: lwIP calls output with an IPv6 packet and the resulting frame has `Protocol = 0x02`.

## 3. Netif — link-local and ULA address

- [ ] 3.1 In `NetifDriver.cpp`, under `ENIGMANG_IPV6_ENABLED`, call `esp_netif_create_ip6_linklocal(mesh0_netif)` after netif creation. Verify: the node obtains `fe80::&lt;EUI-64&gt;/10` on `mesh0`.
- [ ] 3.2 Implement function `buildUlaFromMac(const uint8_t mac[6], esp_ip6_addr_t* out)` that builds the static ULA address: `ENIGMANG_IPV6_ULA_PREFIX` + MAC EUI-64. Verify: unit test with known MAC produces the expected address.
- [ ] 3.3 In `NetifDriver.cpp`, register the static ULA address on `mesh0` via `esp_netif_set_ip6_addr()` if there is no active gateway (or as fallback before receiving an RA). Verify: the ULA address appears in `esp_netif_get_all_ip6()`.

## 4. IPv6 routing table

- [ ] 4.1 Define `struct RouteEntry6` in `Router.h` per design (43 bytes per entry). Add static pool `RouteEntry6 _routes6[MAX_ROUTES6]` with `MAX_ROUTES6 = 32` in `meshConfig.h`. Verify: `sizeof(RouteEntry6) == 43`.
- [ ] 4.2 Implement `Router::addRoute6(const uint8_t dst[16], uint8_t prefixLen, const uint8_t mac[6], const uint8_t nextHop[6], uint8_t hopCount, int8_t rssi, uint8_t flags)` with eviction policy. Verify: unit test inserts 33 routes, the 33rd replaces the one with highest hopCount expired.
- [ ] 4.3 Implement `Router::lookupRoute6(const uint8_t dst6[16], RouteEntry6* out)` with longest-prefix match. Verify: unit test with `/48` and `/128` routes returns the most specific.
- [ ] 4.4 Implement `Router::removeRoute6(const uint8_t mac[6])` to remove routes whose destination MAC or nextHop is the indicated mac. Verify: unit test.
- [ ] 4.5 Integrate `_checkPeerTimeouts()` to expire `_routes6` entries just like IPv4 ones. Verify: an entry with expired TTL disappears from the table.

## 5. IPv6 route advertisement

- [ ] 5.1 Add flag `RA_FLAG_IPV6 = 0x02` to the ROUTE_ADV flags enum in `Router.h`. Verify: the value does not collide.
- [ ] 5.2 In `Router::buildRouteAdv()` (or equivalent), add logic to generate ROUTE_ADV frames with IPv6 entries (24B per entry, 9 per frame) when `ENIGMANG_IPV6_ENABLED`. Apply IPv6 Split Horizon. Verify: frame generated with 9 IPv6 entries has correct length (216B).
- [ ] 5.3 In the received ROUTE_ADV parser, add branch for `RA_FLAG_IPV6`: iterate 24B entries, call `addRoute6()`. Verify: integration test where a node learns IPv6 routes from another node's RA.
- [ ] 5.4 Apply `RA_CONTINUATION` if the IPv6 table has more than 9 entries: emit additional frames. Verify: table with 15 IPv6 entries generates 2 RA frames.

## 6. Gateway — IPv6 router, DHCPv6-PD and RA towards LAN

- [ ] 6.1 In `Gateway.cpp`, add optional IPv6 prefix parameter to `begin()`: `begin(ssid, pass, ip6Prefix = nullptr)`. If `nullptr`, use `ENIGMANG_IPV6_ULA_PREFIX`. Verify: the API compiles and the prefix is stored internally.
- [ ] 6.2 Implement `Gateway::_tryDhcpv6PD()`: minimal DHCPv6-PD client (UDP port 546→547) that sends `SOLICIT` with `IA_PD` option and processes upstream `ADVERTISE`/`REPLY`. 10s timeout; if no response, falls back to static ULA. Verify: on a network with DHCPv6-PD (OpenWrt in test), the gateway obtains a delegated prefix and uses it instead of ULA.
- [ ] 6.3 Implement `Gateway::_sendRouterAdvertisement(esp_netif_t* iface, bool includePrefixInfo, bool includeRIO)`: manually build the ICMPv6 RA packet (type 134). Towards `mesh0`: Prefix Information Option with A-flag=1 (SLAAC). Towards `wifi_sta`: Prefix Information Option with A-flag=0 + Route Information Option (type 24, RFC 4191) with the mesh prefix, Prf=High, lifetime=3600s. Verify: Wireshark/log shows RA with RIO on the WiFi interface.
- [ ] 6.4 Add periodic timer (`IPV6_RA_INTERVAL = 60s`) for RA retransmission on both interfaces. Send unsolicited RA immediately on startup. Verify: RAs are emitted every 60s; Wireshark capture confirms presence of RIO.
- [ ] 6.5 Confirm that `ip_napt_enable()` is NOT called for the IPv6 netif. Verify: IPv6 traffic between node and LAN preserves source/destination IPs without translation.
- [ ] 6.6 Update Web UI documentation (`/api/v1/nodes`) to include each node's IPv6 address. Verify: JSON endpoint includes `ip6` field.
- [ ] 6.7 Add in the Web UI (`/`) an informational block with the active IPv6 mesh prefix, propagation mechanism in use (DHCPv6-PD / RA RIO / manual), and, in fallback mode, the exact static route command for the user's router (`ip -6 route add &lt;prefix&gt; via &lt;gateway-ip&gt;`). Verify: the UI shows correct information in each mode.

## 7. NDP and multicast traffic

- [ ] 7.1 Verify that frames with DstMAC `FF:FF:FF:FF:FF:FF` (broadcast) are used for NDP multicast packets (`ff02::/16`). No additional implementation is required if lwIP handles it automatically, just verify the behavior. Verify: Router Solicitation reaches the gateway via `mesh0`.
- [ ] 7.2 Add `PROTO_IPV6` frame statistics to the telemetry counters (Prometheus endpoint `/metrics`): `enigmang_frames_ipv6_rx_total`, `enigmang_frames_ipv6_tx_total`. Verify: metrics appear in `/metrics` when `ENIGMANG_IPV6_ENABLED`.

## 8. Unit and integration tests

- [ ] 8.1 Create `test/test_ipv6/test_main.cpp` with tests for: `buildUlaFromMac()`, `addRoute6()`, `lookupRoute6()` (longest-prefix match), `removeRoute6()`. Verify: all tests pass in the PlatformIO environment.
- [ ] 8.2 Add integration test: 3 nodes in line A–B–C with IPv6 enabled. A sends ping6 to C. Verify: B forwards the `PROTO_IPV6` frame and C responds.
- [ ] 8.3 Add test: node without `ENIGMANG_IPV6_ENABLED` receives `PROTO_IPV6` frame → silent discard, no crash. Verify: the node continues processing IPv4 frames normally.
- [ ] 8.4 Add test: `RouteEntry6` table with 32 full entries + insertion of entry 33 → correct eviction. Verify: the evicted entry is the one with highest hopCount or the oldest.

## 9. Documentation

- [ ] 9.1 Update `EnigmaNG Specs v2.md` §4.1 (Protocol field): change `PROTO_IPV6` from "Reserved for v1.x" to active. Verify: consistency with link-layer spec.
- [ ] 9.2 Update `EnigmaNG Specs v2.md` §8 (IP netif): add IPv6 subsection with MTU, addressing, and lwIP configuration. Verify: values are consistent with `ipv6-netif` spec.
- [ ] 9.3 Add note in README / Web UI about the need for a static route on the user's WiFi router to reach the IPv6 mesh prefix from the LAN. Verify: the note includes example `ip route add` command.