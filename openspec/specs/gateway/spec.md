# Spec: Gateway WiFi

**Referencia:** §9 de EnigmaNG Specs v2.md

## Propósito

Conectar la red mesh ESP-NOW a la red WiFi/Internet, proporcionando routing bidireccional LAN↔mesh, NAT para Internet, AP de onboarding, Web UI y métricas Prometheus.

## Funciones del gateway

- WiFi STA (conexión al AP externo) + ESP-NOW mesh simultáneos.
- AP de onboarding permanente (segunda MAC del ESP32 en modo AP+STA).
- Servidor DHCP para nodos mesh que no usen IP estática.
- Servidor web: dashboard + API JSON + Prometheus.
- Puede haber múltiples gateways por redundancia.

## Routing: NAT completo (masquerade)

Todo el tráfico saliente de la mesh hacia la red WiFi o Internet pasa por NAT masquerade usando la IP WiFi del gateway. **No se requieren rutas estáticas en el router WiFi del usuario.**

```
Tráfico mesh → LAN WiFi (10.200.x.x → 192.168.1.x):
    NAT masquerade. Los nodos mesh se ven como la IP del gateway
    desde el punto de vista de la LAN. Conexiones outbound funcionan
    de forma transparente (MQTT, HTTP, CoAP, etc.).

Tráfico mesh → Internet (10.200.x.x → 0.0.0.0/0):
    NAT masquerade. Idéntico al caso LAN.
```

- `ip_napt` habilitado en `mesh0` (lwIP NAPT, IDF 5.5.4).
- Todo tráfico saliente desde `mesh0` → `wifi_sta` es masquerado con la IP WiFi.
- **Sin rutas estáticas** en el router WiFi: cero configuración de red requerida al usuario.
- **Trade-off aceptado:** la LAN no puede iniciar conexiones hacia nodos mesh (no hay inbound NAT). Esto es correcto para el caso de uso habitual (nodos publican a broker MQTT / servidor HTTP en LAN o Internet).
- Si se necesita acceso inbound (ej. Web UI del gateway), el propio gateway tiene IP WiFi directa y no pasa por NAT.

## Selección de gateway (routing multi-gateway)

Los gateways anuncian flag `IS_GATEWAY` y métrica en `ROUTE_ADV`:

```
Metric = hopCount × 100 + (100 + wifi_rssi)
```

Los nodos eligen el gateway con menor métrica. Desempate: menor `hopCount`, luego mejor RSSI al nextHop.

Redundancia: si un gateway desaparece, sus rutas expiran y los nodos migran automáticamente. Las conexiones TCP activas se rompen (diferente IP NAT). Aceptable para v1.0.

## Abstracción MeshUplink (para dual-board)

```cpp
class MeshUplink {
public:
    virtual bool begin(const char* ssid, const char* pass) = 0;
    virtual esp_netif_t* getNetif() = 0;
    virtual int8_t getRssi() = 0;
    virtual bool isConnected() = 0;
};

class NativeWifiUplink : public MeshUplink { /* esp_wifi nativo */ };
class HostedWifiUplink : public MeshUplink { /* esp_hosted, IDF only */ };
```

`ENIGMANG_HOSTED_UPLINK` define en compilación qué implementación se usa. La lógica de gateway es idéntica en ambos casos.

## Web UI y autenticación

- **Servidor:** `esp_http_server` (IDF nativo). Descartada AsyncWebServer.
- **Autenticación:** HTTP Digest Auth (RFC 7616). Nativo en `esp_http_server` IDF 5.x.
- El endpoint `/metrics` puede no requerir auth (Prometheus accede desde red interna).

**Endpoints:**

| Método | Path | Descripción |
|--------|------|-------------|
| GET | `/` | Dashboard HTML (topología, nodos, rutas) |
| GET | `/api/v1/status` | JSON: estado general |
| GET | `/api/v1/nodes` | JSON: lista de nodos |
| GET | `/api/v1/routes` | JSON: tabla de rutas |
| GET | `/api/v1/peers` | JSON: tabla de peers y RSSI |
| GET | `/metrics` | Prometheus text format |
| POST | `/api/v1/config` | Cambio de configuración (requiere auth) |

## Métricas Prometheus

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

## Gateway dual-board (ESP-Hosted) — ⚠️ IDF nativo obligatorio

Ver §9.5 del spec principal. Requiere proyecto CMake nativo de ESP-IDF. No compatible con Arduino Core. Se implementa como ejemplo separado en `examples/idf/gateway_hosted/`.

## Criterio de aceptación

- Test: nodo mesh hace HTTP request a `http://example.com` (verifica NAT Internet).
- Test: dispositivo WiFi hace `ping 10.200.0.5` (nodo mesh) a través del gateway (verifica routing LAN).
- Test: Web UI accesible en `http://<gateway_ip>/`. Digest Auth rechaza credenciales incorrectas.
- Test: `/metrics` devuelve texto Prometheus válido.
- Test: gateway primario desaparece. Nodos migran al secundario en < 60s.
