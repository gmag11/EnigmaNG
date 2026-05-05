# Spec: Nodos Batería

**Referencia:** §7 de EnigmaNG Specs v2.md

## Propósito

Soporte para nodos ESP32 alimentados por batería que minimizan el consumo mediante deep sleep cíclico. Modelo inspirado en LoRaWAN Clase A.

## Ciclo de vida (Clase A)

```
[DEEP SLEEP] ──(T_sleep)──▶ [WAKE] ──▶ [TX UPLINK] ──▶ [RX1: 2s] ──▶ [RX2: 2s] ──▶ [SLEEP]
```

- El nodo solo escucha inmediatamente después de transmitir (2 ventanas RX).
- Sin polling activo. Radio apagada fuera de las ventanas.
- `BATTERY_HEARTBEAT_INTERVAL` por defecto: 3600s (1h). Configurable.

## Parent Node

- El nodo batería elige su Parent durante el join: vecino relay con mejor `avgRssi`.
- El Parent almacena un buffer FIFO de downlink por hijo:
  - Máximo 5 mensajes por hijo.
  - Máximo 200 bytes por mensaje.
  - Al recibir el UPLINK, vacía el buffer en ventanas RX1 + RX2.
- Si no llega heartbeat en `3 × HEARTBEAT_INTERVAL`: libera buffer + retira rutas del hijo.

## Sincronización de reloj

El Parent incluye un timestamp (segundos UTC aproximados) en la respuesta al UPLINK. El nodo batería almacena el offset en `RTC_DATA_ATTR` (persiste en deep sleep).

**Usos:**
- Wake-up precisos con `esp_sleep_enable_timer_wakeup()`.
- Verificación de expiración de epoch de clave al despertar.
- Timestamps en telemetría.

## Redescubrimiento de Parent (lista 3 candidatos en NVS)

El nodo batería almacena en NVS hasta **3 candidatos a Parent** (MAC + última `avgRssi`), actualizada en cada UPLINK exitoso.

**Al despertar:**
1. Intenta UPLINK al Parent primario (4s timeout: RX1 + RX2).
2. Si no hay ACK → prueba el segundo candidato (4s).
3. Si no hay ACK → prueba el tercero (4s).
4. Si los tres fallan → búsqueda ciega de canal (§5.2) + re-join completo.

**Coste NVS:** ~20 bytes para 3 entradas `(mac[6], rssi[1]) × 3`.

## Gestión de epoch en nodos batería

Al despertar del deep sleep, el nodo compara el epoch almacenado en `RTC_DATA_ATTR` con el epoch del primer frame recibido del Parent. Si difieren: inicia renegociación de clave antes de enviar datos.

## Modos configurables

| Parámetro | Default | API |
|-----------|---------|-----|
| Sleep interval | 3600s | `setBatteryMode(true, seconds)` |
| Heartbeat interval | 3600s | `BATTERY_HEARTBEAT_INTERVAL` |
| RX1 window | 2s | Constante interna |
| RX2 window | 2s | Constante interna |
| Max downlink buffer por hijo | 5 msgs | `BATTERY_DOWNLINK_BUFFER_SIZE` |

## Criterio de aceptación

- Test: nodo batería cicla cada 60s. Envía UPLINK y recibe respuesta en ventana RX1 o RX2.
- Test: 5 mensajes downlink en buffer. Nodo los recibe todos en las ventanas tras el UPLINK.
- Test: Parent primario desaparece. Nodo recupera conexión vía candidato secundario sin re-join.
- Test: epoch cambia durante el deep sleep. Nodo detecta el cambio al despertar y renegocia antes de enviar datos.
- Test: nodo batería NO actúa como relay (no reenvía frames de otros nodos).
