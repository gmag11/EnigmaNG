# Spec: Capa IP — netif Virtual, MTU, ARP, DHCP, ICMP

**Referencia:** §8 de EnigmaNG Specs v2.md

## Propósito

Exponer la mesh ESP-NOW como interfaz de red IP estándar (`mesh0`) mediante un netif virtual lwIP, permitiendo que cualquier aplicación TCP/UDP funcione sobre la mesh sin modificaciones.

## Interfaz virtual `mesh0`

- Creada con `esp_netif_new()` y driver personalizado (`ESP_NETIF_ID_CUSTOM`).
- **Path de entrada (RX):** Frame `DATA` descifrado → `esp_netif_receive()` → lwIP.
- **Path de salida (TX):** `mesh_netif_output()` → buscar ruta → cifrar → `sendUnicast/Broadcast()`.

## MTU y configuración TCP

```
ESP-NOW payload:   250 bytes
Header L2:          22 bytes
Tag GCM:            12 bytes
──────────────────────────────
MTU (mesh0):       216 bytes
TCP MSS:           176 bytes  (216 - 20 IP - 20 TCP)
UDP max payload:   188 bytes  (216 - 20 IP - 8 UDP)
```

**`lwipopts.h` recomendado:**

```c
#define TCP_MSS              176
#define TCP_WND              (4 * TCP_MSS)   // 704 bytes
#define TCP_SND_BUF          (4 * TCP_MSS)
#define LWIP_TCP_SACK_OUT    0
```

## Fragmentación L2

Para payloads IP > 216B (excepcional — TCP MSS los previene para TCP):

```
[FragID: 2B][FragOffset: 1B][FragFlags: 1B] = 4 bytes overhead
```

- Timeout reensamblaje: 2s.
- Máximo 4 fragmentos por paquete.

## Asignación de IPs

| Modo | Escenario | Mecanismo |
|------|-----------|-----------|
| Estático distribuido (default) | Nodos batería, instalaciones fijas | MAC→IP en NVS; tabla distribuida via ROUTE_ADV |
| DHCP | Nodos conectados con IP dinámica | Servidor DHCP en gateway (lwIP dhcpserver) |
| IP fija manual | Configuración explícita | `begin(psk, IPAddress(...))` |

**Subred:** `10.200.0.0/16` (configurable). Gateway: primera IP asignable.

## ARP

- Tabla de routing unificada (§6) resuelve IP→MAC localmente → sin tráfico ARP para destinos conocidos.
- `ARP_QUERY` broadcast solo si IP no está en tabla.
- `ARP_REPLY` gratuitous enviado en cada join (anuncia IP→MAC al llegar a la mesh).

## ICMP (Ping)

ICMP es completamente transparente — **no requiere ningún cambio en la capa mesh.**

- ICMP es IPv4 protocolo 1 → viaja como `PROTO_IPV4 (0x01)` en el header mesh.
- lwIP responde automáticamente a echo requests en cualquier netif registrado (incluyendo `mesh0`).
- Los relays reenvían el frame DATA como cualquier otro; no tienen visibilidad del protocolo IP interior.

**Tamaño típico de ping:**

```
ICMP echo request: 20B IP + 8B ICMP + 32B datos = 60B payload
Frame total:       22B header + 60B payload + 12B tag = 94 bytes
```

**API `esp_ping` funciona sin configuración adicional:**

```c
esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
config.target_addr.u_addr.ip4.addr = ipaddr_addr("10.200.0.5");
config.target_addr.type = IPADDR_TYPE_V4;
// lwIP selecciona mesh0 automáticamente para IPs 10.200.0.0/16
esp_ping_new_session(&config, &cbs, &ping);
esp_ping_start(ping);
```

## Criterio de aceptación

- Test: `ping` entre dos nodos con IPs estáticas. RTT medido.
- Test: UDP de 200 bytes (requiere fragmentación L2). Reensamblaje correcto en destino.
- Test: TCP MQTT QoS 0 publish funcional entre nodo y broker en LAN.
- Test: `ping 8.8.8.8` desde nodo mesh a través del gateway (mesh → gateway → NAT → Internet).
- Test: `ping <ip_mesh>` desde dispositivo WiFi de la LAN a través del gateway (routing LAN §9.2).
