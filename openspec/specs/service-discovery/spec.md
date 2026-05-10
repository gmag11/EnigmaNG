# Spec: Service Discovery

**Reference:** §12 of EnigmaNG Specs v2.md

## Purpose

Allow mesh nodes to announce and discover services (HTTP, MQTT broker, CoAP, Prometheus) without the verbosity of standard mDNS (unsuitable for a 216-byte MTU).

## Custom protocol over the mesh

Service records are included as an optional field in `ROUTE_ADV`. Format (18 bytes/record):

```
[Service Type: 1B][Port: 2B][Name: 0-15B null-terminated]
```

**Service types:**

| Value | Type |
|-------|------|
| 0x01 | HTTP |
| 0x02 | MQTT broker |
| 0x03 | CoAP |
| 0x04 | Prometheus metrics |

**Resolution:**
1. Node sends `SERVICE_QUERY` broadcast with the requested type.
2. Nodes offering the service reply `SERVICE_REPLY` unicast with IP + port.

## Standard mDNS on the gateway

The gateway republishes mesh services to the WiFi network via standard mDNS (lwIP mDNS):
- Mesh nodes and services become visible to WiFi devices without configuration.
- The gateway advertises `_http._tcp`, `_mqtt._tcp`, `_coap._udp` according to active mesh services.

## Acceptance criteria

- Test: node sends `SERVICE_QUERY` for an MQTT broker. Gateway replies with correct IP:port.
- Test: service announced in `ROUTE_ADV` is visible in the local service table.
- Test: from a WiFi device on the LAN, mDNS discovery of `_mqtt._tcp` returns the mesh broker IP.
