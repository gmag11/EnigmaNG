## ADDED Requirements

### Requirement: DNS proxy server on gateway
The gateway SHALL run a DNS server on UDP port 53 bound to its `mesh0` interface IP address. This server SHALL accept DNS queries (Type A, Class IN only) from mesh nodes. Queries for unknown types or classes SHALL receive a NOERROR response with an empty answer section (pass-through behavior not required for unsupported types).

#### Scenario: Node resolves an internet hostname
- **WHEN** a mesh node sends a DNS query for `example.com` (Type A) to the gateway's mesh IP on UDP/53
- **THEN** the gateway proxies the query to the upstream LAN DNS, returns the A record to the node, and stores the result in cache with the upstream TTL (floored to the minimum TTL floor)

#### Scenario: Node resolves an uncacheable or already-cached hostname
- **WHEN** a mesh node queries a hostname that is present and unexpired in the DNS cache
- **THEN** the gateway responds immediately from cache without contacting the upstream DNS

#### Scenario: Upstream DNS is unreachable
- **WHEN** the upstream DNS server does not respond within 2 seconds
- **THEN** the gateway returns a SERVFAIL response to the querying node

#### Scenario: DNS server bound only to mesh interface
- **WHEN** a host on the WiFi LAN sends a DNS query to the gateway's WiFi IP on UDP/53
- **THEN** the gateway does NOT respond (socket is not bound on the WiFi interface)

#### Scenario: Node uses its current gateway as DNS server
- **WHEN** `dns_setserver(0, gatewayIP)` has been set on the node and the node calls `getaddrinfo()` or any lwIP resolver
- **THEN** the DNS query is sent to the gateway's mesh IP on UDP/53 transparently

### Requirement: DNS cache with TTL expiry and LRU eviction
The gateway SHALL maintain an in-memory DNS A-record cache. Each entry SHALL expire after its TTL (seconds), with a configurable minimum floor (default 60 s). When the cache reaches its maximum capacity (default 64 entries), the least-recently-used entry SHALL be evicted.

#### Scenario: Cache hit within TTL
- **WHEN** a cached entry exists and `millis() < expireMs`
- **THEN** the cached IP is returned and `lastUseMs` is updated to current time

#### Scenario: Cache hit after TTL expiry
- **WHEN** a cached entry exists but `millis() >= expireMs`
- **THEN** the entry is treated as a miss, evicted, and a new upstream query is performed

#### Scenario: Cache full — LRU eviction
- **WHEN** the cache holds `maxEntries` records and a new entry must be stored
- **THEN** the entry with the oldest `lastUseMs` is removed and the new entry is inserted

#### Scenario: Cache flush on custom record update
- **WHEN** the user saves a new custom DNS record list via Web UI or API
- **THEN** the entire in-memory cache is cleared

### Requirement: Custom DNS A records
The gateway SHALL support a list of user-defined DNS A records. Each record SHALL consist of a fully-qualified hostname (max 63 characters) and an IPv4 address. Custom records SHALL take priority over both the cache and upstream lookups. A query matching a custom record SHALL be answered immediately with the configured IP; the upstream DNS SHALL NOT be consulted.

#### Scenario: Custom record for a mesh node
- **WHEN** a mesh node queries `sensor1.mesh` and a custom record `sensor1.mesh → 10.200.0.10` is configured
- **THEN** the gateway responds with `10.200.0.10` without contacting upstream DNS

#### Scenario: Custom record for a LAN host
- **WHEN** a mesh node queries `printer.home` and a custom record `printer.home → 192.168.1.50` is configured
- **THEN** the gateway responds with `192.168.1.50`

#### Scenario: No matching custom record
- **WHEN** a mesh node queries a hostname not in the custom record list
- **THEN** the gateway falls through to the cache and then upstream DNS

### Requirement: Custom record persistence in NVS
The gateway SHALL persist the custom DNS record list in the NVS namespace `"dns"`, key `"records"`, as a JSON string. The list SHALL be loaded at startup and saved atomically on every configuration change.

#### Scenario: Records survive reboot
- **WHEN** custom records are saved and the gateway is rebooted
- **THEN** the same custom records are available immediately after `DnsProxy::begin()`

#### Scenario: Empty list on first boot
- **WHEN** no NVS key exists for `"dns"/"records"`
- **THEN** `DnsProxy::begin()` initialises an empty custom record list and the DNS proxy operates with no custom records

#### Scenario: Corrupt NVS data
- **WHEN** the NVS value for `"dns"/"records"` is not valid JSON
- **THEN** the gateway logs an error and starts with an empty custom record list (fail-safe)

### Requirement: Web UI DNS configuration page
The Web UI SHALL include a DNS management page accessible to authenticated users. The page SHALL display the current custom record list and allow adding and deleting individual records.

#### Scenario: Add a custom record
- **WHEN** an authenticated user submits a new hostname and IP via the Web UI form
- **THEN** the record is added to the list, saved to NVS, and the cache is flushed

#### Scenario: Delete a custom record
- **WHEN** an authenticated user deletes a record by hostname
- **THEN** the record is removed from the list, saved to NVS, and the cache is flushed

#### Scenario: Invalid IP address
- **WHEN** the user submits a record with a malformed IPv4 address
- **THEN** the Web UI returns a 400 error and the record is not saved

### Requirement: REST API for DNS configuration
The gateway SHALL expose REST endpoints for DNS record management, secured by the existing HTTP Digest Auth mechanism.

| Method | Path | Description |
|--------|------|-------------|
| GET | `/api/v1/dns/records` | Returns the full custom record list as JSON |
| POST | `/api/v1/dns/records` | Adds a new record (`{"name":"...", "ip":"..."}`) |
| DELETE | `/api/v1/dns/records/{name}` | Deletes the record with the given hostname |
| GET | `/api/v1/dns/cache` | Returns current cache entries (for diagnostics) |

#### Scenario: GET records returns JSON array
- **WHEN** an authenticated GET request is made to `/api/v1/dns/records`
- **THEN** the response is `Content-Type: application/json` with a JSON array of `{"name":"...","ip":"..."}` objects

#### Scenario: POST record persists and flushes cache
- **WHEN** a valid POST request adds a new record
- **THEN** the record appears in subsequent GET responses, is persisted in NVS, and the DNS cache is flushed

#### Scenario: DELETE removes record
- **WHEN** a DELETE request targets an existing record name
- **THEN** the record is removed, NVS is updated, and the response is 200 OK

#### Scenario: Unauthenticated request rejected
- **WHEN** a request to any `/api/v1/dns/*` endpoint is made without valid credentials
- **THEN** the server returns 401 Unauthorized
