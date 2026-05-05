# Spec: Onboarding y Descubrimiento de Canal

**Referencia:** §5 de EnigmaNG Specs v2.md

## Propósito

Permitir que un nodo nuevo descubra la red mesh, obtenga el canal y los parámetros de configuración, y se una de forma segura.

## Mecanismo principal: AP Permanente en Gateway

El gateway levanta un SoftAP permanente (modo dual STA+AP simultáneo del ESP32, sin penalización de rendimiento significativa para la carga ESP-NOW esperada).

**Configuración del AP:**

```
SSID:     ENIGMA-<NetworkID_HEX>-CH<CANAL>
Password: HMAC-SHA256(PSK, "onboarding")[:8] codificado en hex (16 chars)
```

> La contraseña deriva de la PSK pero no la expone directamente: fuerza bruta del hash no recupera la PSK en tiempo razonable.

**Protocolo de provisioning (HTTP sobre AP):**

```http
GET http://192.168.4.1/provision
→ 200 OK Content-Type: application/json
{
  "network_id": "A1B2",
  "channel": 6,
  "gateway_mac": "AA:BB:CC:DD:EE:FF",
  "ip_range": "10.200.0.0/16",
  "broker": "10.200.0.1:1883"
}
```

Tras recibir la respuesta, el nodo:
1. Desconecta del AP WiFi.
2. Configura ESP-NOW en el canal indicado.
3. Envía `JOIN_BEACON` cifrado con NetworkKey.

## Fallback: Búsqueda ciega de canal

Para nodos ya configurados que se reinician o están fuera de alcance del gateway:

1. WiFi scan buscando SSID `ENIGMA-*`. Si encontrado → usar canal del SSID.
2. Si no hay SSID visible → escanear canales 1 → 6 → 11 → resto, `CHANNEL_DWELL_TIME` = 200ms por canal.
3. Escuchar `JOIN_BEACON` con Magic `0x454E` y NetworkID derivado de la PSK local.
4. Los gateways y relays envían `JOIN_BEACON` cada 5s en broadcast.

## JOIN_BEACON

Enviado en broadcast con NetworkKey. Contenido mínimo:

```
[NetworkID: 2B][Channel: 1B][GatewayMAC: 6B][HopCount: 1B][Flags: 1B]
```

Los relays incluyen el canal en sus `ROUTE_ADV` (campo `channel` en flags), permitiendo que nodos fuera de alcance del gateway reciban el canal.

## Propagación del canal

- Relays incluyen canal actual en `ROUTE_ADV`.
- Prioridad: canal anunciado por el nodo con menor `hopCount` al gateway.

## Criterio de aceptación

- Test: nodo nuevo con PSK correcta conecta via AP, recibe provisioning JSON, configura canal y completa JOIN en < 5s.
- Test: nodo con PSK incorrecta recibe provisioning JSON pero falla el JOIN_BEACON (tag GCM inválido).
- Test: nodo que se reinicia (sin AP visible) encuentra canal via búsqueda ciega en < 30s en canal 6.
- Test: nodo a 2 hops del gateway descubre canal via ROUTE_ADV de relay.
