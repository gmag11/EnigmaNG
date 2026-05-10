# NAT/NAPT Debugging — EnigmaNG Gateway (May 2026)

## Síntoma

Ping desde un nodo mesh a un host de la LAN no llegaba al destino (confirmado con Wireshark). El mismo ping desde el gateway sí funcionaba. lwIP recibía el datagrama pero no salía por la interfaz wifi_sta.

---

## Bugs encontrados y solución

### Bug 1 — Flags incorrectos en la netif mesh0

**Fichero:** `src/NetifDriver.cpp` — `mesh_netif_init()`

mesh0 es una interfaz IP pura (los payloads que inyecta/recibe son datagramas IPv4 crudos, sin cabecera Ethernet). Al añadir los flags `NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET`, `tcpip_input()` detecta que la interfaz es tipo Ethernet y despacha el paquete a `ethernet_input()` en vez de a `ip_input()`. `ethernet_input()` interpreta los primeros 14 bytes del datagrama IP como una cabecera MAC+EtherType, no encuentra 0x0800, y descarta el paquete silenciosamente.

```c
// ❌ Incorrecto
lwip_netif->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP |
                    NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;

// ✅ Correcto — raw IP interface
lwip_netif->flags = NETIF_FLAG_UP | NETIF_FLAG_LINK_UP | NETIF_FLAG_BROADCAST;
```

**Regla:** Solo añadir `NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET` si la interfaz trabaja con tramas Ethernet completas (con cabecera MAC de 14 bytes). Para interfaces que envían/reciben paquetes IP crudos, omitirlos.

---

### Bug 2 — Byte order incorrecto en `ip_napt_enable()`

**Fichero:** `src/MeshNetwork.cpp`

`ip_napt_enable(addr, 1)` compara `addr` con `ip4_addr_get_u32(netif_ip4_addr(netif))` para identificar sobre qué netif activar NAPT. La IP en lwIP se almacena en **network byte order** (big-endian en el campo `addr` de `ip4_addr_t`). En ESP32 (little-endian), una IP como `10.200.80.236` se almacena como `0xEC50C80A`.

La construcción manual `((mesh0[0] << 24) | (mesh0[1] << 16) | ...)` produce `0x0AC850EC` (host byte order), que no coincide → NAPT se activa silenciosamente en ninguna interfaz.

```c
// ❌ Incorrecto — byte order invertido respecto a lwIP
uint32_t mesh0ip = ((uint32_t)mesh0[0] << 24) | ((uint32_t)mesh0[1] << 16) |
                   ((uint32_t)mesh0[2] << 8)  |  (uint32_t)mesh0[3];

// ✅ Correcto — IPAddress::operator uint32_t() devuelve network byte order
uint32_t mesh0ip = (uint32_t)self->getLocalIP();
ip_napt_enable(mesh0ip, 1);
```

**Regla:** `IPAddress::operator uint32_t()` en Arduino/ESP-IDF devuelve la IP en network byte order, igual que `ip4_addr_t::addr` de lwIP. Usar siempre este cast directo; no reconstruir el uint32_t byte a byte desde los octetos.

---

### Bug 3 — `pbuf_alloc` con capa incorrecta para reenvío por interfaz Ethernet

**Fichero:** `src/NetifDriver.cpp` — `injectRxPacket()`

Este fue el bug que impedía la salida del paquete a la LAN incluso cuando los dos anteriores estaban corregidos.

Cuando lwIP reenvía un paquete de mesh0 a wifi_sta (una interfaz Ethernet), `ethernet_output()` necesita **prefijar 14 bytes** de cabecera Ethernet al pbuf mediante `pbuf_header(p, 14)`. Esto solo funciona si el pbuf tiene espacio de cabecera libre antes del payload (`PBUF_LINK_HLEN = 14 bytes`).

```c
// ❌ Incorrecto — PBUF_RAW no reserva headroom; pbuf_header() falla → ERR_BUF
struct pbuf* p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);

// ✅ Correcto — PBUF_LINK reserva 14 bytes de headroom para cabecera Ethernet
struct pbuf* p = pbuf_alloc(PBUF_LINK, len, PBUF_POOL);
```

**Regla:** Usar siempre `PBUF_LINK` (o `PBUF_IP` si la interfaz de salida es también IP-only) al inyectar paquetes en lwIP para reenvío. `PBUF_RAW` es adecuado únicamente cuando el paquete se entrega a la aplicación (socket recv), no cuando puede atravesar capas adicionales.

| Capa pbuf | Headroom reservado | Uso típico |
|---|---|---|
| `PBUF_TRANSPORT` | TCP/UDP + IP + Ethernet | Creación de segmentos |
| `PBUF_IP` | IP + Ethernet | Inyección en capa IP |
| `PBUF_LINK` | Ethernet (14 B) | Inyección de paquetes IP para reenvío |
| `PBUF_RAW` | 0 | Recepción en aplicación / raw sockets |

---

## Interfaz correcta para activar NAPT

La documentación de Espressif es explícita:

> *"NAPT must be enabled on the interface connecting to the **target** network."*
> Ejemplo: para que tráfico Ethernet salga por WiFi, NAPT se habilita en la interfaz **Ethernet** (la interna/fuente).

En EnigmaNG: NAPT se habilita en **mesh0** (la interfaz interna donde llegan los paquetes de los nodos). Al recibir un paquete en mesh0 con `napt=1`, lwIP reescribe el source IP al IP de wifi_sta antes de enviarlo.

Habilitar NAPT en wifi_sta (la interfaz externa/uplink) es incorrecto y no produce reenvío.

---

## Camino completo del paquete (cuando funciona)

```
Nodo                          Gateway (ESP32)
────                          ───────────────
pingOnce() raw socket
  → ip_output()               
  → mesh_netif_output()       
  → MeshNetwork::_netifTxCallback()
  → _sendFrameVia(gwMAC)      
  ──── ESP-NOW ────────────►  _handleData()
                               → injectRxPacket()  [PBUF_LINK]
                               → tcpip_input()
                               → ip_input()         [mesh0, napt=1]
                               → ip4_forward()
                               → ip_napt_forward()  [rewrite src IP → 192.168.5.105]
                               → ethernet_output()  [prepend 14B Ethernet hdr]
                               → wifi_sta TX
                               ──── WiFi ──────────► LAN host
```
