# Spec: Routing (Distance Vector)

**Referencia:** §6 de EnigmaNG Specs v2.md

## Propósito

Routing proactivo multi-hop sobre la mesh ESP-NOW, basado en Distance Vector (estilo RIPv2), con prevención de bucles mediante Seen-Frame Cache + Split Horizon + Poison Reverse.

## Tabla de routing unificada (IP ↔ MAC ↔ NextHop)

```cpp
struct RouteEntry {
    uint32_t ip;          // 4B — IPv4 destino
    uint8_t  mac[6];      // 6B — MAC destino final
    uint8_t  nextHop[6];  // 6B — MAC siguiente salto
    uint8_t  hopCount;    // 1B — 0=local, 255=Poison Reverse
    int8_t   rssi;        // 1B — RSSI al nextHop
    uint32_t lastUpdate;  // 4B — millis()
    uint16_t ttl;         // 2B — segundos hasta expirar
    uint8_t  flags;       // 1B — IS_GATEWAY|IS_BATTERY|IS_DIRECT
};
// sizeof(RouteEntry) = 25 bytes
// Pool estático: 64 entradas = 1.600 bytes
```

## Route Advertisement (ROUTE_ADV)

- **Intervalo base:** `RA_INTERVAL` = 30s.
- **Triggered update:** Cambio de topología → RA inmediato + reinicio timer.
- **Formato de entrada (12 bytes cada una):**
  ```
  [IPv4: 4B][MAC_destino: 6B][HopCount: 1B][Flags: 1B]
  ```
  El `nextHop` no se incluye: es el emisor del RA.
- **Capacidad por frame:** 18 entradas IPv4 (216B / 12B = 18 exacto → frame de 250B exactos).
- **Continuación:** Si tabla > 18 entradas, múltiples RA con flag `RA_CONTINUATION`.

## TTL de rutas

| Tipo | TTL por defecto |
|------|----------------|
| Vecino directo | 90s |
| 2–4 hops | 180s |
| 5+ hops | 300s |

Preset `MESH_MOBILE_MODE`: TTL 90s para todos + `RA_INTERVAL` = 15s.

## Prevención de bucles — Seen-Frame Cache

```cpp
struct SeenFrame {
    uint8_t  srcMac[6];   // 6B
    uint16_t seq;         // 2B
    uint32_t timestamp;   // 4B
};
// Buffer circular: 32 entradas × 12B = 384 bytes por nodo relay
```

Un relay descarta el frame si `(srcMac, seq)` está en la caché (`SEEN_FRAME_TTL` = 10s). Si no, lo añade y lo reenvía.

## Mecanismos DVR estándar

1. **Split Horizon:** No anunciar en RA hacia peer X las rutas con `nextHop == X`.
2. **Poison Reverse:** Rutas aprendidas de X se anuncian a X con `hopCount = 255`.
3. **TTL IP:** lwIP decrementa TTL; paquetes con TTL=0 descartados.

## Route Withdraw

- Si peer no responde en `PEER_TIMEOUT` (3600s nodos normales; `3 × HEARTBEAT_INTERVAL` nodos batería): enviar `ROUTE_WITHDRAW` broadcast con la MAC del peer caído.
- Optimización: solo enviar si el peer era `nextHop` de alguna ruta activa.

## Control de memoria

- Pool estático de `MAX_ROUTES` (default 64, `#define` en compilación).
- Evicción cuando tabla llena: 1) entradas expiradas → 2) mayor hopCount → 3) menos recientemente actualizada.

## Criterio de aceptación

- Test: 3 nodos en línea A–B–C. A envía DATA a C. Verificar que B retransmite y C recibe (1 frame, no duplicados).
- Test: 5 nodos, desconectar nodo central. Reconvergencia en < 60s (2 intervalos RA).
- Test: Seen-Frame Cache descarta duplicados cuando B recibe el mismo frame por dos paths.
- Test: frame con `hopCount = 255` no genera ruta en tabla.
- Test: ROUTE_WITHDRAW elimina entradas correspondientes en todos los vecinos.
