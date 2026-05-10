# Spec: Gateway WiFi

**Reference:** §9 of EnigmaNG Specs v2.md

## Purpose

Connect the ESP-NOW mesh network to WiFi/Internet, providing bidirectional LAN↔mesh routing, NAT for Internet, onboarding AP, Web UI and Prometheus metrics.

## Gateway features

- WiFi STA (connection to external AP) + ESP-NOW mesh simultaneously.
- Permanent onboarding AP (second MAC of the ESP32 in AP+STA mode).
- DHCP server for mesh nodes that don't use a static IP.
- Web server: dashboard + JSON API + Prometheus.
- Multiple gateways can exist for redundancy.

## Routing: full NAT (masquerade)

All outbound traffic from the mesh toward the WiFi network or Internet is NAT masqueraded using the gateway's WiFi IP. **No static routes are required on the user's WiFi router.**

```
Mesh traffic → WiFi LAN (10.200.x.x → 192.168.1.x):
    NAT masquerade. Mesh nodes appear as the gateway's IP
    from the LAN perspective. Outbound connections work
    transparently (MQTT, HTTP, CoAP, etc.).

Mesh traffic → Internet (10.200.x.x → 0.0.0.0/0):
    NAT masquerade. Identical to the LAN case.
```

- `ip_napt` enabled on `mesh0` (lwIP NAPT, IDF 5.5.4).
- All outbound traffic from `mesh0` → `wifi_sta` is masqueraded with the WiFi IP.
- **No static routes** on the WiFi router: zero network configuration required from the user.
- **Trade-off accepted:** the LAN cannot initiate connections toward mesh nodes (no inbound NAT). This fits the common use case (nodes publish to an MQTT broker / HTTP server on LAN or Internet).
- If inbound access is needed (e.g., the gateway's Web UI), the gateway itself has a direct WiFi IP and is not behind NAT.

## Gateway selection (multi-gateway routing)

Gateways announce `IS_GATEWAY` flag and a metric in `ROUTE_ADV`:

```
Metric = hopCount × 100 + (100 + wifi_rssi)
```

Nodes choose the gateway with the lowest metric. Tie-break: lower `hopCount`, then better RSSI to nextHop.

Redundancy: if a gateway disappears, its routes expire and nodes migrate automatically. Active TCP connections break (different NAT IP). Acceptable for v1.0.

## MeshUplink abstraction (for dual-board)

```cpp
class MeshUplink {
public:
    virtual bool begin(const char* ssid, const char* pass) = 0;
    virtual esp_netif_t* getNetif() = 0;
    virtual int8_t getRssi() = 0;
    virtual bool isConnected() = 0;
};

class NativeWifiUplink : public MeshUplink { /* native esp_wifi */ };
class HostedWifiUplink : public MeshUplink { /* esp_hosted, IDF only */ };
```

`ENIGMANG_HOSTED_UPLINK` compile-time define selects which implementation is used. Gateway logic is identical in both cases.

## Web UI and authentication

- **Server:** `esp_http_server` (native IDF). AsyncWebServer discarded.
- **Authentication:** HTTP Digest Auth (RFC 7616). Native in `esp_http_server` IDF 5.x.
- `/metrics` endpoint may not require auth (Prometheus scrapes from internal network).

**Endpoints:**

| Method | Path | Description |
|--------|------|-------------|
| GET | `/` | Dashboard HTML (topology, nodes, routes) |
| GET | `/api/v1/status` | JSON: overall status |
| GET | `/api/v1/nodes` | JSON: node list |
| GET | `/api/v1/routes` | JSON: routing table |
| GET | `/api/v1/peers` | JSON: peers table and RSSI |
| GET | `/metrics` | Prometheus text format |
| POST | `/api/v1/config` | Change configuration (requires auth) |

## Prometheus metrics

```
mesh_nodes_total{network="XXXX"} 15
mesh_routes_total{network="XXXX"} 32
mesh_link_rssi{src="AABBCCDDEEFF",dst="112233445566"} -72
mesh_key_epoch{link="..."} 42
mesh_uptime_seconds{node="AABBCCDDEEFF"} 86400
mesh_battery_voltage{node="AABBCCDDEEFF"} 3.65
mesh_packets_total{node="AABBCCDDEEFF",direction="rx"} 15000
mesh_heap_free{node="AABBCCDDEEFF"} 45312
mesh_route_convergence_ms{network="XXXX"} 250
```

## Dual-board gateway (ESP-Hosted) — ⚠️ native IDF required

See §9.5 of the main spec. Requires native ESP-IDF CMake project. Not compatible with Arduino Core. Implemented as a separate example in `examples/idf/gateway_hosted/`.

## Acceptance criteria

- Test: a mesh node makes an HTTP request to `http://example.com` (verifies Internet NAT).
- Test: a WiFi device can `ping 10.200.0.5` (mesh node) through the gateway (verifies LAN routing).
- Test: Web UI accessible at `http://<gateway_ip>/`. Digest Auth rejects wrong credentials.
- Test: `/metrics` returns valid Prometheus text.
- Test: primary gateway disappears. Nodes migrate to secondary in < 60s.
