## Context

EnigmaNG mesh nodes do not use DHCP — IP addresses are assigned statically via MAC→IP derivation or NVS table (§8 of EnigmaNG Specs v2.md). Because there is no DHCP exchange, DNS server information cannot be distributed via DHCP Option 6. Nodes have no usable DNS server today and must hard-code IP addresses in application code. However, every node already tracks its best gateway: `Router::getBestGateway()` returns the selected gateway entry, and `NetifDriver::setDefaultGateway()` is called whenever the gateway changes. This call site is the natural place to also configure the DNS server. The gateway already handles all outbound IP traffic via NAT, making it the correct DNS endpoint. The upstream DNS server for the LAN is available at runtime via `esp_netif_get_dns_info()`.

## Goals / Non-Goals

**Goals:**
- DNS proxy on the gateway listening on UDP/53 on the `mesh0` interface IP.
- TTL-based DNS cache (A records only) to reduce LAN DNS traffic.
- Custom A records stored in NVS, editable from the Web UI, that override or supplement upstream responses.
- Nodes automatically configure the selected gateway's mesh IP as their DNS server (`dns_setserver()`) when gateway selection changes, with no DHCP required.
- Memory-bounded cache (configurable max entries, default 64).

**Non-Goals:**
- AAAA (IPv6), SRV, PTR, or any record type beyond A.
- DNSSEC, DoH, DoT.
- mDNS integration or coexistence beyond what already exists.
- Serving DNS to WiFi LAN clients — only mesh nodes are served.
- Zone files or zone transfer.

## Decisions

### D1: Raw UDP socket vs. DNS library

**Decision:** Implement a minimal DNS packet parser/builder in-house using POSIX sockets (`lwip_socket`).

**Rationale:** No suitable embedded DNS server library exists for IDF 5.x with lwIP that handles both server and proxy roles without significant flash overhead. The DNS wire format for A queries is straightforward: a fixed 12-byte header followed by the question section. Processing only Type A (1) / Class IN (1) queries limits the parser to ~150 lines of C++.

**Alternative considered:** Use `esp-idf-lib`'s DNS component — rejected because it does not support acting as a proxy or custom records.

### D2: Task architecture for DNS server

**Decision:** Dedicate a single FreeRTOS task (`xDnsTask`, stack 3072 B, priority 4) that blocks on `recvfrom()` and processes one query at a time.

**Rationale:** DNS queries on a small mesh are infrequent (< 10/s peak). A blocking receive loop is the simplest model; no async I/O framework is needed. The task is created by `DnsProxy::begin()` and deleted by `DnsProxy::stop()`.

**Alternative considered:** Integrate into the main `loop()` with `select()` polling — rejected to avoid stalling the event loop.

### D3: Cache data structure

**Decision:** `std::vector<DnsCacheEntry>` with linear scan. Eviction policy: LRU (least recently used) when the cache is full.

```
DnsCacheEntry (20 bytes each):
  uint32_t ip;         // 4 B — resolved A record
  uint32_t expireMs;   // 4 B — millis() when entry expires
  uint32_t lastUseMs;  // 4 B — millis() of last cache hit (for LRU)
  char     name[...];  // up to 63 B (compressed to fixed 64-char slot)
```

Total per entry (name field fixed to 64 B for O(1) slot access): **76 bytes**. 64 entries = **4864 bytes** on heap.

**Rationale:** At 64 entries, std::map overhead (tree nodes + allocations) is not warranted. A vector with LRU eviction is cache-friendly and trivially bounded.

### D4: Custom record storage in NVS

**Decision:** Store the full custom record list as a JSON string in NVS namespace `"dns"`, key `"records"` (`nvs_set_str` / `nvs_get_str`).

```json
[{"name":"sensor1.mesh","ip":"10.200.0.10"},{"name":"myhost.lan","ip":"192.168.1.50"}]
```

**Rationale:** JSON is human-readable and easy to parse with ArduinoJson (already a transitive dependency via WebUI). The list is expected to be small (< 32 records), so a ~1 kB NVS blob is acceptable.

**Alternative considered:** Binary packed format — rejected: saves only ~20 B per record but adds serialization complexity.

### D5: Upstream relay strategy

**Decision:** For each cache-missing query, open a new UDP socket to the upstream DNS, send the original question, await response with a 2-second timeout, close the socket.

**Rationale:** DNS over UDP is stateless; reusing a bound socket would require demultiplexing concurrent queries. Since the server is single-threaded (D2), sequential resolution is correct. The 2-second timeout matches common stub resolver defaults.

**Alternative considered:** Keep a persistent upstream socket — complicates error recovery when the WiFi STA reconnects and the upstream IP changes.

### D6: DNS server propagation via gateway-selection hook

**Decision:** Call `dns_setserver(0, gatewayIP)` inside `NetifDriver::setDefaultGateway()` (or in `MeshNetwork` immediately after that call) whenever the node's best gateway changes.

**Rationale:** There is no DHCP in the mesh. The existing gateway-selection path already calls `setDefaultGateway()` on every gateway change, making it the single authoritative hook. Piggybacking `dns_setserver()` here requires zero new signalling — the node always mirrors its IP default route and DNS server to the same gateway. `dns_setserver()` is a pure lwIP call (`#include <lwip/dns.h>`) with no RTOS overhead.

**Alternative considered:** Distribute DNS server via a new mesh control message — rejected as unnecessary complexity when the gateway IP is already known locally.

### D7: Custom records override upstream

**Decision:** If a query name matches a custom record, the gateway responds immediately with the custom IP and does NOT forward to upstream.

**Rationale:** Custom records are intentionally authoritative for that name. Forwarding to upstream first would defeat their purpose (e.g., overriding a public hostname for local routing).

## Risks / Trade-offs

| Risk | Mitigation |
|------|------------|
| Upstream DNS unreachable (WiFi STA down) | Return SERVFAIL to client immediately; do not block the DNS task indefinitely. Timeout is 2 s (D5). |
| Cache serves stale entries | Expiry checked on every cache hit (`millis() > expireMs`). Expired entries are evicted before responding. |
| NVS write on every Web UI save | NVS writes are wear-leveled and the list is small. Acceptable for configuration frequency (not real-time). |
| ArduinoJson not available in IDF-only build | Guard `#include <ArduinoJson.h>` with `ENIGMANG_ARDUINO` or implement a minimal JSON serializer for the NVS blob in the IDF path. |
| DNS amplification if gateway is reachable from LAN | The DNS socket binds only to the `mesh0` IP (not 0.0.0.0), so LAN clients cannot reach it. |

## Migration Plan

1. `NetifDriver::setDefaultGateway()` (or the call site in `MeshNetwork`) gains a `dns_setserver(0, gw)` call — transparent to existing applications; if `enableDns()` is not called on the gateway the DNS queries simply fail as before.
2. `DnsProxy` is opt-in via `Gateway::enableDns()` — existing users who do not call it retain the old behaviour.
3. No NVS namespace conflicts with existing keys (new namespace `"dns"`).

## Open Questions

- **Minimum TTL floor:** Should uncached entries from upstream with TTL < 60 s be clamped to 60 s to reduce upstream load? Proposed default: yes, floor = 60 s, configurable.
- **Cache invalidation on custom record edit:** When the user saves a new custom record list via Web UI, should the in-memory cache be fully flushed? Proposed: yes, full flush on save.
- **dns_setserver index:** lwIP supports up to `DNS_MAX_SERVERS` (typically 2) entries. Index 0 is used here to match the primary slot. No conflict expected with IDF's internal DNS configuration for the STA interface (that uses a separate netif-level resolver path).
