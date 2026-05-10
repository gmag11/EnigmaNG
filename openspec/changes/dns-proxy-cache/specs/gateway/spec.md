## ADDED Requirements

### Requirement: Gateway DNS service lifecycle
The gateway SHALL expose `enableDns()` and `disableDns()` methods on the `Gateway` class. `enableDns()` SHALL start the `DnsProxy` on UDP/53 bound to the gateway's `mesh0` IP and shall remain running until `disableDns()` or `stop()` is called. No DHCP restart is required; mesh nodes automatically direct DNS queries to the gateway when their gateway selection changes (see ip-netif spec).

#### Scenario: DNS enabled
- **WHEN** `gateway.enableDns()` is called
- **THEN** the DNS proxy begins listening on UDP/53 on the mesh0 IP; mesh nodes that have selected this gateway already use its IP as their DNS server via `dns_setserver()`

#### Scenario: DNS not enabled by default
- **WHEN** `gateway.enableDns()` has NOT been called
- **THEN** no DNS server is started; mesh nodes will have their DNS server address configured to the gateway IP but queries will be refused (ICMP port-unreachable) until DNS is enabled

### Requirement: Web UI DNS page
The gateway Web UI SHALL include a `/dns` HTML page listing current custom DNS records and providing a form to add or delete records. The page SHALL require Digest Auth.

#### Scenario: DNS page accessible to authenticated users
- **WHEN** an authenticated GET request is made to `http://<gateway_ip>/dns`
- **THEN** the server returns an HTML page listing custom DNS records and the add/delete form

## MODIFIED Requirements

### Requirement: Gateway Web UI endpoints
The gateway SHALL serve the following HTTP endpoints (updated list):

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Dashboard HTML (topology, nodes, routes) |
| GET | `/dns` | DNS configuration page (requires auth) |
| GET | `/api/v1/status` | JSON: overall status |
| GET | `/api/v1/nodes` | JSON: node list |
| GET | `/api/v1/routes` | JSON: routing table |
| GET | `/api/v1/peers` | JSON: peers table and RSSI |
| GET | `/api/v1/dns/records` | JSON: custom DNS record list |
| POST | `/api/v1/dns/records` | Add a custom DNS record (requires auth) |
| DELETE | `/api/v1/dns/records/{name}` | Delete a custom DNS record (requires auth) |
| GET | `/api/v1/dns/cache` | JSON: current DNS cache entries (diagnostic) |
| GET | `/metrics` | Prometheus text format |
| POST | `/api/v1/config` | Change configuration (requires auth) |

#### Scenario: DNS page requires authentication
- **WHEN** an unauthenticated GET request is made to `/dns`
- **THEN** the server returns 401 Unauthorized with a `WWW-Authenticate: Digest` header
