# Tasks: dns-proxy-cache — DNS Proxy and Cache on Gateway

## Progress: 0/18 tasks completed

---

## Phase 1: Mocks and infrastructure

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md`

- [ ] 1.1 Add POSIX socket mock stubs (`lwip_socket`, `lwip_bind`, `lwip_sendto`, `lwip_recvfrom`, `lwip_close`) to `test/mocks/` so native unit tests compile
  - _Test: `pio test -e native` compiles without linker errors after this task_

- [ ] 1.2 Add `esp_netif_get_dns_info()` stub to `test/mocks/esp_netif.h` returning a configurable upstream DNS IP
  - _Test: `pio test -e native` compiles; stub returns the configured IP_

---

## Phase 2: DNS packet parser/builder

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md` — DNS proxy server requirement

- [ ] 2.1 Create `src/DnsProxy.h` and `src/DnsProxy.cpp` with a minimal DNS wire-format parser: read the 12-byte DNS header, extract the question name, type (A=1), and class (IN=1); reject other types with NOERROR/empty answer
  - _Test: unit test in `test/test_dns_proxy/` parses a captured A-query byte array and returns the correct hostname_

- [ ] 2.2 Implement a DNS response builder that takes a transaction ID, question name, and an IPv4 answer, and produces a valid wire-format DNS response
  - _Test: unit test verifies the built response parses correctly using the same parser; Wireshark can decode it (manual check)_

- [ ] 2.3 Implement SERVFAIL response builder (for upstream timeout / error cases)
  - _Test: unit test verifies the RCODE field in the built response is 2 (SERVFAIL)_

---

## Phase 3: DNS cache

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md` — DNS cache with TTL expiry and LRU eviction

- [ ] 3.1 Implement `DnsCache` class (`std::vector<DnsCacheEntry>`, max 64 entries): `lookup(name)`, `insert(name, ip, ttlSeconds)`, `evictExpired()`, and LRU eviction on insertion when full. Entry struct: `ip(4B) + expireMs(4B) + lastUseMs(4B) + name[64B]` = 76 bytes/entry
  - _Test: unit test inserts 64 entries, inserts a 65th, verifies the LRU entry was evicted_

- [ ] 3.2 Add configurable minimum TTL floor (default 60 s): TTL values below the floor are clamped before storing
  - _Test: unit test inserts entry with TTL=5 s and floor=60 s; `entry.expireMs` is `now + 60000 ms`_

- [ ] 3.3 Implement `flush()` method that clears all entries; call it from `DnsProxy` when custom records are saved
  - _Test: unit test calls `flush()` after inserting entries; `lookup()` returns no results_

---

## Phase 4: Custom DNS records and NVS persistence

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md` — Custom DNS A records and NVS persistence

- [ ] 4.1 Implement `DnsCustomRecords` class: holds a `std::vector<DnsRecord>` (name + IPv4), provides `lookup(name)`, `add(name, ip)`, `remove(name)`, and `getAll()`
  - _Test: unit test adds, looks up, and removes records; lookup is case-insensitive_

- [ ] 4.2 Implement NVS serialization: `save()` writes JSON to `"dns"/"records"` via `nvs_set_str`; `load()` reads and parses JSON via ArduinoJson; malformed JSON logs error and returns empty list
  - _Test: unit test with NVS mock writes records, reads them back, and gets identical results; corrupted JSON returns empty list without crash_

---

## Phase 5: Upstream DNS relay

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md` — DNS proxy server (upstream relay)

- [ ] 5.1 Implement `DnsProxy::relayToUpstream(queryPacket, len, upstreamIp)`: opens UDP socket, sends the original query to upstream:53, waits up to 2 s with `select()`, reads response or returns `false` on timeout/error
  - _Test: unit test with a loopback UDP echo server verifies query bytes are forwarded and response is returned within timeout_

- [ ] 5.2 Obtain upstream DNS IP at runtime from `esp_netif_get_dns_info(wifi_sta_netif, ESP_NETIF_DNS_MAIN)` and cache it in `DnsProxy`; refresh whenever the WiFi STA reconnects
  - _Test: unit test verifies that after calling `setUpstreamDns(ip)` the relay sends to that IP_

---

## Phase 6: DNS server task

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md` — DNS proxy server requirement

- [ ] 6.1 Implement `DnsProxy::begin(meshIp)`: binds a UDP socket to `meshIp:53`; creates a FreeRTOS task (`xDnsTask`, stack 3072 B, priority 4) that loops on `recvfrom()`, processes each query (custom records → cache → upstream relay), and sends the response
  - _Test: integration test on hardware: a mesh node resolves `example.com` and receives a valid A record within 3 s_

- [ ] 6.2 Implement `DnsProxy::stop()`: signals the task to exit (flag + unblocks socket by closing it), deletes the task, and closes the socket
  - _Test: calling `stop()` followed by `begin()` again does not crash or leak sockets (stress-test 5 cycles on hardware)_

---

## Phase 7: Node-side DNS server configuration

**Spec:** `openspec/changes/dns-proxy-cache/specs/ip-netif/spec.md` — Node configures DNS server from selected gateway

- [ ] 7.1 Add `#include <lwip/dns.h>` to `NetifDriver.cpp`; inside `NetifDriver::setDefaultGateway()` (after `netif_set_gw`), call `dns_setserver(0, &gw_addr)` to configure the gateway's mesh IP as the primary DNS server in lwIP
  - _Test: unit test calls `setDefaultGateway(IPAddress(10,200,0,1))` and verifies the mock records a `dns_setserver` call with index 0 and the correct IP_

- [ ] 7.2 Add `Gateway::enableDns()` that calls `DnsProxy::begin(meshIp)` — no DHCP restart required
  - _Test: calling `enableDns()` on a running gateway starts the DNS proxy task; nodes automatically direct queries to the gateway because task 7.1 set their DNS server on gateway selection_

---

## Phase 8: Gateway integration

**Spec:** `openspec/changes/dns-proxy-cache/specs/gateway/spec.md` — Gateway DNS service lifecycle

- [ ] 8.1 Add `DnsProxy _dnsProxy` member to `Gateway`; wire `enableDns()` / `disableDns()` lifecycle methods; call `stop()` in `Gateway::stop()`
  - _Test: `pio run -e esp32` compiles without warnings; gateway stop/restart cycle does not leak memory (heap checked before/after)_

- [ ] 8.2 Update `GatewaySingleChip` example to show optional `gateway.enableDns()` call with a comment explaining the feature
  - _Test: example compiles; comment present in source_

---

## Phase 9: REST API

**Spec:** `openspec/changes/dns-proxy-cache/specs/dns-proxy-cache/spec.md` — REST API for DNS configuration

- [ ] 9.1 Register `GET /api/v1/dns/records` handler in `WebUI`: returns JSON array of custom records; requires Digest Auth
  - _Test: HTTP GET to the endpoint with correct credentials returns `200 OK` and a valid JSON array; without credentials returns `401`_

- [ ] 9.2 Register `POST /api/v1/dns/records` handler: parses `{"name":"...","ip":"..."}`, validates IP (returns 400 on invalid), adds record, saves to NVS, flushes cache
  - _Test: POST with valid data → 200; subsequent GET lists the new record; POST with invalid IP → 400_

- [ ] 9.3 Register `DELETE /api/v1/dns/records/{name}` handler: removes matching record, saves NVS, flushes cache; returns 404 if not found
  - _Test: DELETE existing → 200; DELETE non-existing → 404; GET confirms removal_

- [ ] 9.4 Register `GET /api/v1/dns/cache` handler (diagnostic, no auth required): returns JSON array of current cache entries with `name`, `ip`, `ttlRemainingS` fields
  - _Test: after resolving a hostname, GET returns an entry with the correct name and a positive TTL_

---

## Phase 10: Web UI DNS page

**Spec:** `openspec/changes/dns-proxy-cache/specs/gateway/spec.md` — Web UI DNS page

- [ ] 10.1 Register `GET /dns` handler in `WebUI`: returns HTML page listing custom records table and add/delete form; requires Digest Auth
  - _Test: browser navigates to `/dns` with credentials; page renders without JS errors and shows the current record list_

- [ ] 10.2 Wire the Web UI form to call `POST /api/v1/dns/records` (add) and `DELETE /api/v1/dns/records/{name}` (delete) via fetch; show success/error feedback
  - _Test: adding a record via the form and refreshing the page shows the new entry in the table_

---
