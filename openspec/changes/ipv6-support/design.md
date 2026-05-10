## Context

EnigmaNG uses lwIP through `esp_netif` with a virtual `mesh0` interface (MTU 216B). The IPv4 stack is fully operational. The frame `Protocol` field already reserves `PROTO_IPV6 (0x02)` for future use. lwIP in IDF 5.5.4 supports IPv6 natively in `esp_netif`.

The change consists of activating the IPv6 plane: configuring `mesh0` with IPv6 addresses, IPv6 routing table, prefix advertisement from the gateway, and dispatch of `PROTO_IPV6` frames in the link layer.

## Goals / Non-Goals

**Goals:**

- Automatic link-local address (`fe80::/10`) derived from the MAC EUI-64 on each node.
- Routable ULA address (`fd00::/8`, prefix configurable in `meshConfig.h`) or global delegated by the upstream gateway.
- Independent IPv6 routing table in the Router, with proactive advertisement.
- Gateway acting as IPv6 router: propagates the mesh prefix to upstream automatically via DHCPv6-PD and/or RA with Route Information Option (RFC 4191). No manual configuration on the user's router when the mechanism is supported.
- Correct dispatch of `PROTO_IPV6` frames in `LinkLayer`.
- Transparent NDP (Neighbor Discovery): lwIP handles it internally over `mesh0`.

**Non-Goals:**

- Stateful DHCPv6.
- IPv6-only (IPv4 remains mandatory).
- IPv6 support on ESP8266.
- IPv6 traffic filtering/firewall on the gateway.
- Upstream dynamic routing protocols (RIPng, OSPFv3, BGP).
- Full Path MTU Discovery (PMTUD) (see risks).

## Decisions

### D1 — Separate IPv6 routing table

**Decision:** Add an independent `RouteEntry6` table, do not extend `RouteEntry`.

```cpp
struct RouteEntry6 {
    uint8_t  dst[16];      // 16B — destination IPv6 address
    uint8_t  prefixLen;    //  1B — prefix length (128 for hosts)
    uint8_t  mac[6];       //  6B — final destination MAC
    uint8_t  nextHop[6];   //  6B — next hop MAC
    uint8_t  hopCount;     //  1B
    int8_t   rssi;         //  1B
    uint32_t lastUpdate;   //  4B
    uint16_t ttl;          //  2B
    uint8_t  flags;        //  1B — IS_GATEWAY|IS_BATTERY|IS_DIRECT
};
// sizeof(RouteEntry6) = 43 bytes
// Static pool: 32 entries = 1,376 bytes
```

**Discarded alternative:** Extending `RouteEntry` with an `ip6[16]` field (unified struct). Penalizes the memory of the existing IPv4 table (goes from 25B to 45B per entry × 64 = +1,280B). Not justified given that most nodes will have few IPv6 routes.

### D2 — Reuse of ROUTE_ADV frame with IPv6 flag

**Decision:** Reuse the existing `ROUTE_ADV` frame by adding an `RA_FLAG_IPV6` flag in the flags field. The format of each IPv6 entry in the RA goes from 12B to 24B:

```
IPv6 entry: [IPv6_dst: 16B][MAC_dest: 6B][HopCount: 1B][Flags: 1B]
= 24 bytes/entry → 9 entries per frame (216B / 24B = 9)
```

**Discarded alternative:** New frame type `ROUTE_ADV6`. Implies changes in the frame type table, more dispatch complexity, and uses a value from the limited `FrameType` space (only 4 bits free up to 0x0F). The flag in the existing field is more economical.

### D3 — Automatic link-local address

**Decision:** Derive `fe80::/10` from EUI-64: expand the 6B MAC to 8B with `FF:FE` in the middle, invert the Universal/Local bit (bit 7 of the first octet). This is standard EUI-64 (RFC 4291 §2.5.1). lwIP in IDF 5.x can generate it automatically if `LWIP_IPV6_AUTOCONFIG` is enabled or via `esp_netif_create_ip6_linklocal()`.

### D4 — Routable address: static ULA or delegated via DHCPv6-PD

**Decision:** The gateway is the sole prefix assigner for routable addresses. Two modes:

1. **Static ULA (default):** Prefix `fd00::/48` configurable in `meshConfig.h` (`ENIGMANG_IPV6_ULA_PREFIX`). The host-part is derived from the MAC (EUI-64). No external infrastructure required.
2. **Delegated via DHCPv6-PD (RFC 3633):** The gateway requests a prefix from the upstream router acting as a DHCPv6-PD client. The upstream router delegates a `/48` or `/56` to the gateway, which sub-allocates a `/64` for the mesh. Since the upstream router is the one delegating, the route is installed automatically — no manual configuration.

**DHCPv6-PD compatibility:** OpenWrt, Fritz!Box, ISPs with native IPv6, pfSense/OPNsense. Not supported on most basic home routers.

### D4b — Automatic propagation of the mesh prefix to upstream: RA with Route Information Option

**Decision:** When DHCPv6-PD is not available, the gateway includes a **Route Information Option (RIO, RFC 4191, type 24)** in the ICMPv6 RAs it emits towards the LAN (`wifi_sta`). The RIO advertises the mesh prefix with `Prf=High` and configurable lifetime, allowing routers that implement RFC 4191 to install the route automatically.

```
ICMPv6 RA → LAN:
  - Prefix Information Option: mesh prefix, A-flag=0 (no SLAAC for the LAN)
  - Route Information Option (type 24): mesh prefix, Prf=High, lifetime=3600s
```

**RA RIO compatibility:** Linux (radvd, NetworkManager with `accept_ra_rt_info_max_plen`), OpenWrt, modern routers. Not supported on Windows (ignores RIO) nor on most home routers.

**Gateway preference order:**
1. DHCPv6-PD (global prefix, automatic route on upstream)
2. RA with RIO (ULA prefix, automatic route if the router supports it)
3. Fallback: document static route command for unsupported routers

**Discarded alternative:** RIPng/OSPFv3/BGP as upstream routing protocol. Disproportionate complexity for an embedded IoT device with 520KB DRAM.

### D5 — No IPv6 NAT

**Decision:** IPv6 never uses NAT (NAPT). Nodes have globally routable addresses within the mesh. Traffic to the LAN or Internet uses direct routing. The user's WiFi router must know the mesh prefix.

**Rationale:** IPv6 NAT exists (`ip6tables MASQUERADE`) but violates the end-to-end principle of IPv6 and is discouraged by the IETF. For typical EnigmaNG use cases (IoT nodes publishing to MQTT, HTTP), routing with a ULA prefix is functionally equivalent to IPv4 NAT but with bidirectional connectivity (the LAN can initiate connections to the nodes, unlike the IPv4-NAT case).

### D6 — MTU and IPv6 fragmentation

The IPv6 header is 40B (vs 20B IPv4):

```
MTU mesh0:          216 bytes (unchanged)
TCP MSS IPv6:       156 bytes  (216 - 40 IPv6 - 20 TCP)
UDP max payload:    168 bytes  (216 - 40 IPv6 - 8 UDP)
```

IPv6 requires a minimum MTU of 1280B (RFC 8200). `mesh0` has MTU=216B, which formally violates this requirement. lwIP handles fragmentation in software even if the MTU is smaller, but some nodes/routers may reject packets if they receive an ICMPv6 "Packet Too Big" with Next-Hop MTU &lt; 1280B.

**Mitigation:** Document the limitation. For IoT traffic (MQTT, CoAP, sensor data), payloads are ≤168B, so in practice fragmentation is not needed. TCP connections to the outside (which negotiate MSS) work without issue. `LWIP_IPV6_FRAG` is enabled in `lwipopts.h` to cover edge cases.

## Risks / Trade-offs

- **[MTU &lt; 1280B violates RFC 8200]** → Mitigation: document as a known limitation. In practice IoT use cases do not generate large packets. Enable `LWIP_IPV6_FRAG`.
- **[Limited automatic propagation compatibility]** → DHCPv6-PD and RA RIO do not work on all home routers. Mitigation: mechanism hierarchy (PD → RIO → manual fallback). The gateway Web UI shows the exact static route command for the fallback. On user-controlled networks (home lab, industry) the static route is simple to configure.
- **[Additional RAM memory]** → 1,376B for RouteEntry6 table (32 entries). Acceptable on ESP32 (520KB DRAM). Does not apply on ESP8266 (no IP stack).
- **[ROUTE_ADV IPv6: only 9 entries per frame]** → vs 18 for IPv4. Large networks (&gt;9 nodes) require multi-frame RA (existing `RA_CONTINUATION` flag). Manageable impact.
- **[NDP Multicast]** → NDP uses multicast (`ff02::1`, `ff02::2`, solicited-node multicast). These packets will be sent as broadcast frames in the mesh (DstMAC = `FF:FF:FF:FF:FF:FF`). May increase control traffic in large networks. Mitigation: lwIP limits NDP frequency; acceptable for v1.x.

## Migration Plan

1. Feature-flagged: `#define ENIGMANG_IPV6_ENABLED` in `meshConfig.h` (default: disabled in v1.0, enabled in v1.x).
2. Activate in `NetifDriver` the `esp_netif_create_ip6_linklocal()` calls and ULA prefix configuration if the flag is active.
3. The `RouteEntry6` table is allocated only if `ENIGMANG_IPV6_ENABLED`.
4. Unit tests for the new IPv6 routing table and for `PROTO_IPV6` frame dispatch.
5. No frame format changes nor public API changes for IPv4-only nodes (backward compatible).

## Open Questions

- Should the gateway send RAs towards the mesh (so that nodes configure the routable address via SLAAC), towards the WiFi LAN (with RIO and Prefix Information), or in both directions? → Proposal: both, with different options per interface.
- Should the ULA prefix be user-configurable in `begin()` or is `meshConfig.h` sufficient? → Proposal: `meshConfig.h` for the default, override in `begin()`.
- Should the gateway always try DHCPv6-PD and fall back to ULA if there is no response in N seconds? → Proposal: yes, 10s timeout, then static ULA.
- Compatibility with ESP32-C3/S3 (has native IPv6 in IDF 5.x)? → No known blockers, same API. DHCPv6-PD client implementation in lwIP is available in IDF 5.x (`esp_netif_dhcpc_start_v6()` if exposed, or custom implementation over UDP port 546/547).