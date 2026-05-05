# Spec: Descubrimiento de Servicios

**Referencia:** §12 de EnigmaNG Specs v2.md

## Propósito

Permitir que los nodos de la mesh anuncien y descubran servicios (HTTP, MQTT broker, CoAP, Prometheus) sin la verbosidad de mDNS estándar (inadecuado para MTU de 216 bytes).

## Protocolo propio sobre mesh

Los registros de servicios se incluyen como campo opcional en `ROUTE_ADV`. Formato (18 bytes/registro):

```
[Service Type: 1B][Port: 2B][Name: 0-15B null-terminated]
```

**Tipos de servicio:**

| Valor | Tipo |
|-------|------|
| 0x01 | HTTP |
| 0x02 | MQTT broker |
| 0x03 | CoAP |
| 0x04 | Prometheus metrics |

**Resolución:**
1. Nodo envía `SERVICE_QUERY` broadcast con el tipo buscado.
2. Nodos que ofrecen el servicio responden `SERVICE_REPLY` unicast con IP + puerto.

## mDNS estándar en el gateway

El gateway republica los servicios mesh hacia la red WiFi vía mDNS estándar (lwIP mDNS):
- Nodos y servicios mesh son visibles para dispositivos WiFi sin configuración.
- El gateway anuncia `_http._tcp`, `_mqtt._tcp`, `_coap._udp` según los servicios mesh activos.

## Criterio de aceptación

- Test: nodo envía `SERVICE_QUERY` para MQTT broker. Gateway responde con IP:puerto correcto.
- Test: servicio anunciado en `ROUTE_ADV` es visible en la tabla de servicios local.
- Test: desde dispositivo WiFi en la LAN, descubrimiento mDNS de `_mqtt._tcp` devuelve la IP del broker mesh.
