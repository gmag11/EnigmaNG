# Diseño: EnigmaNG v1.0

**Referencia completa:** EnigmaNG Specs v2.md (fichero en raíz del repositorio)

## Arquitectura de capas

```
┌────────────────────────────────────────────────────────────┐
│  APLICACIÓN                                                │
│  PubSubClient / HTTPClient / cualquier lib TCP estándar    │
│  Web UI + Prometheus (solo gateways)                       │
├────────────────────────────────────────────────────────────┤
│  TRANSPORTE (lwIP)    TCP / UDP   MSS=176, WND=4×MSS       │
├────────────────────────────────────────────────────────────┤
│  RED IP (Layer 3)   netif "mesh0"   MTU=216   IPv4         │
│  DHCP server/client / tabla estática distribuida           │
├────────────────────────────────────────────────────────────┤
│  ROUTING (Layer 2.5)   DVR (RIPv2-like)                    │
│  RouteEntry×64=1.6KB   SeenFrameCache×32=384B              │
│  Split Horizon + Poison Reverse + ROUTE_WITHDRAW           │
├────────────────────────────────────────────────────────────┤
│  ENLACE (Layer 2)    22B header + 12B tag = 34B overhead   │
│  AES-128-GCM   Curve25519 ECDH   HKDF-SHA256               │
│  PeerEntry×16=576B (crece con hash table abierta)          │
├────────────────────────────────────────────────────────────┤
│  FÍSICA (Layer 1)   QuickESPNow   RSSI EWMA α=0.3          │
└────────────────────────────────────────────────────────────┘
```

## Decisiones de diseño clave

Todas las decisiones están justificadas en §1–§16 del spec. Aquí se resume la motivación:

| Decisión | Referencia | Clave de la justificación |
|----------|-----------|--------------------------|
| AES-128-GCM (no 256) | §4.2 | 128 bits suficientes; clave 2× más pequeña en PeerEntry |
| Tag 12 bytes (no 16) | §4.2 | NIST SP 800-38D mínimo; ahorra 4B/frame |
| Nonce derivado, no transmitido | §4.1.3 | Ahorra 12B/frame; unicidad por (Epoch, Seq, SrcMAC) |
| Seen-Frame Cache (no campo Path) | §6.4 | Ahorra hasta 49B/frame; 384B RAM fija |
| Epoch + Rechazo (no overlap) | §4.3 | Sin doble clave en RAM; latencia 100ms × 1/día |
| DVR proactivo (no OLSR/BATMAN) | §6.1 | Simple, probado, escalable hasta ~100 nodos |
| AP permanente (no temporal) | §5.1 | ESP32 soporta AP+STA; onboarding inmediato |
| Routing LAN + NAT (no NAT puro) | §9.2 | Acceso bidireccional LAN↔mesh |
| HTTP Digest Auth (no Basic/JWT) | §11.2 | Sin password en claro; sin TLS/JWT en v1.0 |
| Protocol field 1B (EtherType-like) | §4.1.6 | Multiplexación L3; 256 valores suficientes |

## Presupuesto de RAM (ESP32)

| Componente | RAM |
|-----------|-----|
| PeerManager (16 peers × 36B, crece dinámicamente) | ~576 B |
| RouteTable (64 entries × 25B) | ~1.600 B |
| Seen-Frame Cache (32 entries × 12B) | ~384 B |
| Downlink buffer batería (5 nodos × 5 msg × 200B) | ~5.000 B |
| lwIP heap | ~20.000 B |
| Crypto (handshake, temporal) | ~2.000 B |
| Protocol dispatcher (~8 handlers × 8B) | ~64 B |
| **Total estimado** | **~30 KB** |

ESP32 tiene 320KB DRAM. Margen suficiente para la aplicación del usuario.

## Frame header (22 bytes)

```
Offset  Campo       Tamaño  Descripción
──────────────────────────────────────────────────────
0       Magic       2B      0x454E ("EN")
2       Version     1B      0x01
3       NetworkID   2B      HKDF(PSK, "netid", len=2)
5       FrameType   1B      enum FrameType (0x01–0x0F)
6       Protocol    1B      0x00=MESH_INTERNAL, 0x01=IPv4, ...
7       Epoch       1B      clave actual (0–255)
8       SrcMAC      6B      emisor end-to-end
14      DstMAC      6B      receptor end-to-end (FF×6=broadcast)
20      Sequence    2B      anti-replay + componente nonce
22      [Payload]   ≤216B   AES-128-GCM cifrado
X       Tag         12B     GCM Auth Tag
```

## Handshake ECDH (§4.2)

```
A ──KEY_EXCH_HELLO(pubA, nonceA)──────────────────────▶ B
A ◀─KEY_EXCH_REPLY(pubB, nonceB)───────────────────────  B

  SharedSecret = X25519(privA_eph, pubB_eph)
  LinkKey = HKDF(IKM=SharedSecret, salt=PSK, info="link"||macA||macB, len=16)

A ──KEY_EXCH_CONFIRM(GCM{nonceA XOR nonceB})──────────▶ B
A ◀─KEY_EXCH_CONFIRM(GCM{nonceB XOR nonceA})───────────  B
```

## Ciclo de nodo batería (§7)

```
[DEEP SLEEP] ──T_sleep──▶ [WAKE]
                              │
                          TX UPLINK ──▶ [Parent guarda timestamp]
                              │
                          [RX1: 2s] ◀── downlink buffer vaciado
                              │
                          [RX2: 2s] ◀── si quedó pendiente
                              │
                          [DEEP SLEEP]
```

## Gateway: routing policy (§9.2)

```
dst en 10.200.0.0/16?  ──No──▶  dst en subred LAN?  ──No──▶  NAT masquerade
        │                                │
        ▼ (mesh0)                        ▼ (wifi_sta, routing directo)
  Tabla RouteEntry                  ip_forward lwIP
```

## Estructura del código fuente

Ver spec `public-api/spec.md` para la estructura de directorios completa.

Los módulos son independientes y se pueden compilar por separado. El módulo `Gateway` solo se compila si `MESH_GATEWAY_ENABLED` está definido, ahorrando flash en nodos simples.

## Estructura monorepo ESP32 + ESP8266

**Decisión:** `MeshNode8266` se incluye en este repositorio bajo el mismo directorio `src/`, usando preprocesador para separar el código por plataforma. Razones:

1. Es el enfoque estándar de las librerías Arduino multi-plataforma (WiFiManager, AsyncMqttClient, etc.).
2. Compatible con el registro de PlatformIO y Arduino Library Manager, que esperan un único `src/`.
3. Las constantes de protocolo (header de 22B, `FrameType`, `Protocol`, `NetworkID`) están en el mismo directorio — un único punto de verdad.

```
src/
  Protocol.h          ← constantes compartidas ESP32+ESP8266 (sin guards)
  MeshNetwork.h/cpp   ← #ifndef ESP8266 ... #endif
  Router.h/cpp        ← #ifndef ESP8266 ... #endif
  Crypto.h/cpp        ← #ifndef ESP8266 ... #endif
  NetifDriver.h/cpp   ← #ifndef ESP8266 ... #endif
  ... resto módulos ESP32 ...
  MeshNode8266.h      ← API pública (sin guards; seguro incluir desde ambas plataformas)
  MeshNode8266.cpp    ← #ifdef ESP8266 ... #endif
examples/
  arduino/
    MeshNode8266/     ← Ejemplo ESP8266
```

Convención de guards en cada archivo:

```cpp
// Archivos ESP32-only (MeshNetwork.cpp, Router.cpp, etc.)
#ifndef ESP8266
// ... implementación completa
#endif  // ESP8266

// MeshNode8266.cpp
#ifdef ESP8266
// ... implementación ESP8266
#endif  // ESP8266
```

`Protocol.h` no necesita guards — solo enums y structs válidos en ambas plataformas. PlatformIO selecciona la plataforma mediante el `platform` del environment; el compilador excluye el código irrelevante a través del preprocesador:

```ini
[env:esp8266-MeshNode8266]
platform = espressif8266
framework = arduino
; build_src_filter no necesario — el preprocesador hace la selección
```

## Dependencias externas

| Librería | Versión | Uso |
|---------|---------|-----|
| QuickESPNow | última estable | Capa física ESP-NOW |
| mbedTLS | IDF 5.5.4 built-in | Curve25519, HKDF, AES-GCM |
| lwIP | IDF 5.5.4 built-in | Stack TCP/IP, netif, DHCP, mDNS |
| esp_http_server | IDF 5.5.4 built-in | Web UI + Digest Auth |
| esp_ping | IDF 5.5.4 built-in | Diagnóstico (transparente) |
| Unity | PlatformIO built-in | Framework de tests |
