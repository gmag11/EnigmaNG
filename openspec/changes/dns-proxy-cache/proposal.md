## Why

Mesh nodes currently have no DNS resolution: they must hard-code IP addresses or implement their own lookup, which is brittle and user-unfriendly. The gateway already routes all outbound IP traffic, making it the natural place to intercept and serve DNS queries transparently. Adding a DNS proxy+cache on the gateway also enables assigning human-readable names to mesh nodes and LAN hosts without touching the user's router.

## What Changes

- The gateway runs a DNS server (UDP/53) visible to all mesh nodes via `mesh0`.
- DHCP responses to mesh nodes advertise the gateway's mesh IP as the DNS server.
- The gateway proxies DNS queries to the upstream DNS server obtained from the WiFi STA (LAN).
- Resolved entries are cached with their original TTL (minimum configurable floor, default 60 s).
- A configurable list of custom DNS records can override or add entries (A records only for v1.0).
  - Custom records point to mesh node IPs, LAN host IPs, or any IPv4 address.
  - Configuration is persisted in NVS.
- Custom DNS records are manageable from the Web UI (list / add / delete).
- New Web UI page and REST endpoints for DNS configuration.

## Capabilities

### New Capabilities

- `dns-proxy-cache`: DNS proxy and cache service running on the gateway, serving mesh nodes, with configurable custom A records persisted in NVS.

### Modified Capabilities

- `gateway`: DNS server address added to DHCP responses for mesh nodes; new Web UI page and API endpoints for DNS management.
- `ip-netif`: DHCP server option 6 (DNS) must be set to the gateway's mesh IP.

## Impact

- **New source files:** `DnsProxy.cpp` / `DnsProxy.h` (DNS server, cache, upstream relay).
- **Modified:** `Gateway.cpp` (start/stop `DnsProxy`), `WebUI.cpp`/`WebUI.h` (DNS config page + API), `NetifDriver.cpp` (DHCP option 6).
- **NVS namespace:** `dns` — stores serialized custom record list (JSON or binary).
- **No router/LAN config changes required** — DNS is transparent to the rest of the network.
- **No impact on ESP8266** — feature is gateway-only.

## No-goals

- DNS for protocols other than A records (AAAA, SRV, mDNS coexistence, DNSSEC, etc.) — out of scope for v1.0.
- Authoritative DNS zone files or zone transfer.
- Split-horizon DNS (different answers for LAN vs. mesh clients).
- DNS over HTTPS / DNS over TLS.
- Pushing DNS config to nodes via mesh control messages — nodes receive DNS server via DHCP only.
