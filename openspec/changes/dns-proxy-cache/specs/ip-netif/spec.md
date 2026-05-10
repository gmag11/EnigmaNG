## ADDED Requirements

### Requirement: Node configures DNS server from selected gateway
When a mesh node selects or changes its best gateway (via `Router::getBestGateway()` / `NetifDriver::setDefaultGateway()`), the node's IP stack SHALL be configured to use that gateway's mesh IP as its DNS server by calling `dns_setserver(0, gatewayIP)`. No DHCP is required; the DNS server follows gateway selection automatically.

#### Scenario: Node gains its first gateway
- **WHEN** a node receives a `ROUTE_ADV` that establishes its first valid gateway and `setDefaultGateway()` is called
- **THEN** `dns_setserver(0, gatewayIP)` is called immediately so that subsequent DNS queries target the gateway's mesh IP

#### Scenario: Node switches to a better gateway
- **WHEN** a node's best gateway changes and `setDefaultGateway()` is called with a new IP
- **THEN** `dns_setserver(0, newGatewayIP)` is called, replacing the previous DNS server entry

#### Scenario: No gateway available
- **WHEN** the node has no valid gateway route
- **THEN** no `dns_setserver()` call is made; DNS queries will fail until a gateway is selected (expected behaviour)
