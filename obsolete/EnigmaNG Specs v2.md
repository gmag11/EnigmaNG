# EnigmaNG — Especificaciones Técnicas v2.0

**Versión:** 2.0 — Especificación revisada con análisis y justificación de decisiones de diseño  
**Target:** Arduino Core ESP32 v3.3.8 (IDF 5.5.4) / ESP8266 (librería derivada)  
**Base física:** QuickESPNow + ESP-NOW  
**Referencia arquitectónica:** EnigmaIOT

---

## Resumen de cambios respecto a v1.0

| Área | Cambio |
| ------ | -------- |
| Cifrado | AES-128-GCM (no AES-256) — justificado en §4.2 |
| Tag MIC | 12 bytes (no 16) — misma seguridad práctica |
| Frame header | 22 bytes (añadido campo Protocol), eliminado campo Path — ver §4.1 |
| Loop prevention | Cache de frames vistos — elimina hasta 49 bytes por frame |
| MTU | 216 bytes efectivos (recalculado con nuevo header) |
| Rotación de clave | Epoch + rechazo + renegociación (sin ventana overlap) — §4.3 |
| RSSI EWMA | α=0.3, sin ventana temporal arbitraria — §3.3 |
| Onboarding AP | Permanente en gateways alimentados — §5.1 |
| Gateway routing | Routing LAN + NAT Internet (híbrido) — §9.2 |
| Nodo batería | Lista de 3 Parents en NVS para redescubrimiento eficiente — §7.5 |
| ESP8266 | Broker configurado en gateway y distribuido via beacon — §10.2 |
| Certificados | Pospuesto a v1.1 — §13 |
| mDNS | Protocolo propio ligero, mDNS estándar solo en interfaz WiFi del gateway |
| ICMP/Ping | Transparente sin cambios; `esp_ping` funciona sobre `mesh0` — §8.5 |
| Gateway dual-board | ESP-Hosted como opción avanzada; elimina restricción de canal — §9.5 |

---

## 1. Stack Tecnológico y Versiones

| Componente | Versión / Notas | Justificación |
| ------------ | ---------------- | --------------- |
| **Arduino Core ESP32** | 3.3.8 (IDF 5.5.4) | Versión estable más reciente con API nativa completa |
| **Arduino Core ESP8266** | Última estable | Librería derivada separada |
| **Capa física** | QuickESPNow | Abstracción limpia sobre ESP-NOW, desarrollada por el autor |
| **ECDH** | Curve25519 (mbedTLS) | Claves de 32 bytes, ~100ms en ESP32, resistente a side-channel. Alternativa P-256 descartada por ser más lenta y necesitar aritmética de curvas más compleja |
| **KDF** | HKDF-SHA256 (RFC 5869) | Estándar probado para derivación de claves a partir de shared secret |
| **Cifrado unicast** | AES-128-GCM (hw acelerado) | 128 bits de seguridad, suficiente para el horizonte temporal de IoT. AES-256 no aporta seguridad práctica adicional en este contexto y duplica el tamaño de clave en RAM |
| **Cifrado broadcast** | AES-128-GCM | Unificado con unicast para simplificar la implementación. ChaCha20 requiere clave de 256 bits por diseño (no existe variante de 128 bits), lo que penalizaría el tamaño de clave en el PeerManager |
| **MAC/Auth Tag** | GCM tag truncado a 12 bytes | NIST SP 800-38D permite truncado mínimo a 12 bytes. Ahorra 4 bytes por frame sin pérdida de seguridad para mensajes de baja tasa |
| **Stack IP** | lwIP via esp_netif | API nativa IDF, sin dependencias externas |
| **Web UI (gateway)** | esp_http_server (IDF nativo) | Evita dependencia de AsyncWebServer; compatibilidad garantizada con IDF 5.5.4 |
| **Almacenamiento claves** | NVS (ESP32) / EEPROM (ESP8266) | NVS tiene wear leveling nativo en ESP32. SPIFFS descartado para credenciales (overhead de sistema de archivos innecesario) |

> **Nota sobre Curve25519 vs P-256:** Aunque IDF incluye aceleración hardware para P-256 (via esp_dsa), Curve25519 tiene una implementación en mbedTLS más compacta, opera sobre el campo primo 2²⁵⁵-19 que facilita aritmética eficiente en software, y es la curva estándar de TLS 1.3 y Signal Protocol. Para handshakes esporádicos la diferencia de velocidad es irrelevante.

---

## 2. Arquitectura de Capas

```text
┌─────────────────────────────────────────────────────────────┐
│  APLICACIÓN                                                 │
│  - Clientes MQTT (PubSubClient, esp_mqtt_client)            │
│  - HTTP, Descubrimiento de servicios, NTP-like              │
│  - Web UI + Prometheus (solo gateways)                      │
├─────────────────────────────────────────────────────────────┤
│  TRANSPORTE (lwIP)                                          │
│  - TCP / UDP estándar (MSS configurado a 176 bytes)         │
├─────────────────────────────────────────────────────────────┤
│  RED IP (Layer 3) — Interfaz Virtual "mesh0"                │
│  - IPv4 (v1.0), estructuras preparadas para IPv6 (v1.x)    │
│  - netif custom (esp_netif)                                 │
│  - MTU: 216 bytes                                           │
│  - DHCP server/client / Tabla estática distribuida          │
├─────────────────────────────────────────────────────────────┤
│  ROUTING + ARP (Layer 2.5)                                  │
│  - Protocolo DVR (Distance Vector, estilo RIPv2 ligero)     │
│  - Tabla unificada IP↔MAC↔NextHop                           │
│  - Loop prevention: Split Horizon + Poison Reverse +        │
│    caché de frames duplicados (Seen-Frame Cache)            │
│  - Pools estáticos con evicción LRU                         │
├─────────────────────────────────────────────────────────────┤
│  ENLACE (Layer 2)                                           │
│  - Frame format 21B header + 12B MIC                        │
│  - Cifrado: AES-128-GCM (unicast y broadcast)               │
│  - Handshake ECDH Curve25519 + autenticación PSK (HKDF)     │
│  - Rotación de clave via epoch + renegociación              │
│  - PeerManager con evicción por presión de memoria          │
├─────────────────────────────────────────────────────────────┤
│  FÍSICA (Layer 1)                                           │
│  - QuickESPNow (abstracción ESP-NOW)                        │
│  - RSSI con EWMA (α=0.3) + hysteresis                       │
│  - Canal único sincronizado con AP WiFi del bridge          │
│  - Broadcast / Unicast / Fragmentación L2                   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Capa Física (QuickESPNow / ESP-NOW)

### 3.1 Abstracción mínima requerida

```cpp
class MeshPhysicalLayer {
public:
    bool begin(uint8_t channel, const uint8_t* networkId);
    bool sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len);
    bool sendBroadcast(const uint8_t* data, size_t len);
    void onReceive(MeshRecvCallback cb);
    int8_t getLastRssi();     // RSSI del último frame recibido
    void setChannel(uint8_t channel);
    bool setTxPower(int8_t power);
};
```

### 3.2 Gestión de Canal

- **Canal único:** Toda la red mesh opera en un canal WiFi (1-14).
- **Bridge WiFi:** La mesh opera en el mismo canal que el AP al que el gateway está conectado. Esto es un requisito del hardware ESP32 (radio compartida WiFi + ESP-NOW).
- **Cambio de canal:**
  1. El bridge anuncia `CONTROL/CHANNEL_CHANGE` con timestamp de migración (+30s por defecto).
  2. Todos los nodos cambian de canal. Las claves de enlace se conservan; la conectividad se re-verifica con el próximo frame cifrado.
  3. Nodos que pierden contacto durante el cambio inician búsqueda ciega (§5.2).

> **Nota — restricción de canal en modo single-chip:** La restricción de canal único es inherente al hardware ESP32: WiFi y ESP-NOW comparten la misma radio y deben operar en el mismo canal. Esta limitación **desaparece completamente en el modo gateway dual-board** (ESP-Hosted, ver §9.5), donde la radio WiFi y la radio ESP-NOW son físicamente independientes.

### 3.3 RSSI y Umbral de Alcance

- **Conexión:** RSSI ≥ `RSSI_CONNECT_THRESHOLD` (default: -75 dBm).
- **Desconexión:** RSSI < `RSSI_DISCONNECT_THRESHOLD` (default: -85 dBm).
- **Hysteresis:** 10 dB entre umbrales. Justificación: previene flapping cuando el nodo está en el límite de alcance. Valor estándar en protocolos de roaming WiFi (IEEE 802.11r).
- **Medición con EWMA (α = 0.3):**

  ```text
  rssi_avg = α × rssi_nuevo + (1 - α) × rssi_avg
  ```

  **Justificación frente al original (ventana temporal de 15 min):** La ventana temporal arbitraria introduce complejidad de gestión (timestamps, expiración de muestras) sin beneficio real. El EWMA con α=0.3 pondera el pasado (~3-5 muestras efectivas) de forma natural, decayendo automáticamente las muestras antiguas. Es el estándar en filtrado de señal para sistemas embebidos. Se añade `inactivity_timeout`: si no se recibe ningún frame en `PEER_INACTIVITY_TIMEOUT` (default: 120s), se invalida el RSSI promedio.

---

## 4. Capa de Enlace (Layer 2)

### 4.1 Formato de Frame

#### 4.1.1 Diseño del header y presupuesto de bytes

ESP-NOW limita el payload a **250 bytes**. El diseño del header es crítico. Se eliminan campos con overhead excesivo:

| Campo eliminado respecto a v1.0 | Ahorro | Alternativa |
| -------------------------------- | -------- | ------------- |
| `Path Len` + `Path[0..8]` | 1–49 B | Seen-Frame Cache (§6.4) |
| Reducción Network ID de 4→2 B | 2 B | 16 bits = 65.536 redes, suficiente |
| Reducción MIC de 16→12 B | 4 B | NIST SP 800-38D mínimo 12 bytes |

| Campo añadido | Coste | Razón |
| --------------- | ------- | ------- |
| `Protocol` (1 B) | 1 B | Multiplexación de protocolos L3, equivalente a EtherType en Ethernet |

#### 4.1.2 Header resultante

```text
[Magic:      2 bytes]   // 0x454E ("EN")
[Version:    1 byte]    // 0x01
[Network ID: 2 bytes]   // Truncado de HKDF(PSK, "netid")
[Frame Type: 1 byte]    // Ver enum
[Protocol:   1 byte]    // Identificador de protocolo L3 (ver §4.1.6). Solo relevante para Frame Type DATA/DATA_FRAG
[Epoch:      1 byte]    // Epoch de clave (0–255, wrap-around)
[Src MAC:    6 bytes]   // MAC del emisor original
[Dst MAC:    6 bytes]   // MAC del receptor final (FF:FF:FF:FF:FF:FF = broadcast)
[Sequence:   2 bytes]   // Contador por enlace, anti-replay y componente del nonce
[Payload:    variable]  // Cifrado con AES-128-GCM
[Tag:        12 bytes]  // GCM Authentication Tag
```

**Overhead fijo: 22 bytes header + 12 bytes tag = 34 bytes**  
**Payload disponible: 216 bytes**

> **Nota sobre `Protocol` en frames de control:** Para frames que no son DATA/DATA_FRAG (KEY_EXCH_*, ROUTE_ADV, CONTROL, etc.) el campo `Protocol` se fija a `0x00` (MESH_INTERNAL) y es ignorado por el receptor. La multiplexación L3 solo aplica a frames de datos, evitando sobrecarga innecesaria en los caminos de control.
> **Nota sobre Src/Dst MAC:** Aunque ESP-NOW conoce la MAC del hop inmediato, los campos Src/Dst del header son las MACs del emisor y receptor **finales** (end-to-end), necesarias para el routing multi-hop. El relay usa la MAC del header para enrutar, mientras que ESP-NOW usa la MAC del siguiente salto.

#### 4.1.3 Construcción del nonce AES-128-GCM (no transmitido)

```text
Nonce (12 bytes) = Epoch(1B) || Sequence(2B) || SrcMAC(6B) || 0x000000(3B)
```

> El campo `Protocol` no forma parte del nonce; no contribuye a la unicidad de la clave de cifrado. Sí forma parte del Additional Data (AD) de AES-GCM junto con los demás campos del header, por lo que está autenticado criptográficamente y no puede ser alterado sin invalidar el tag.

El nonce no se transmite porque ambos extremos pueden reconstruirlo a partir del header. Esto ahorra 12 bytes frente a implementaciones que transmiten el nonce explícitamente. La unicidad está garantizada por (Epoch, Sequence, SrcMAC).

#### 4.1.4 Frame Types

| Valor | Nombre | Cifrado con |
| ------- | -------- | ------------ |
| 0x01 | `JOIN_BEACON` | Network Key |
| 0x02 | `KEY_EXCH_HELLO` | Plano (autenticado con Network Key tag) |
| 0x03 | `KEY_EXCH_REPLY` | Plano (autenticado con Network Key tag) |
| 0x04 | `KEY_EXCH_CONFIRM` | Link Key (valida el handshake) |
| 0x05 | `DATA` | Link Key (unicast) / Network Key (broadcast) |
| 0x06 | `DATA_FRAG` | Link Key / Network Key |
| 0x07 | `ROUTE_ADV` | Network Key |
| 0x08 | `ROUTE_WITHDRAW` | Network Key |
| 0x09 | `ARP_QUERY` | Network Key |
| 0x0A | `ARP_REPLY` | Link Key |
| 0x0B | `DHCP_REQUEST` | Network Key |
| 0x0C | `DHCP_REPLY` | Link Key |
| 0x0D | `CONTROL` | Network Key |
| 0x0E | `PROXY_MSG` | Link Key |
| 0x0F | `KEY_NACK` | Network Key (epoch incorrecto) |

#### 4.1.6 Identificadores de Protocolo L3 (`Protocol` field)

El campo `Protocol` de 1 byte funciona como el **EtherType** de Ethernet (IEEE 802.3): permite al receptor demultiplexar el payload cifrado hacia el handler de protocolo correcto sin necesidad de inspeccionar el contenido del frame. Los relays lo reenvían sin interpretarlo.

| Valor | Identificador | Descripción |
| ------- | -------------- | ------------- |
| `0x00` | `PROTO_MESH_INTERNAL` | Mensajes internos de la mesh (control, routing). Valor por defecto en frames no-DATA |
| `0x01` | `PROTO_IPV4` | Paquete IPv4 encapsulado (stack lwIP). Protocolo principal de v1.0 |
| `0x02` | `PROTO_IPV6` | Paquete IPv6 (reservado para v1.x) |
| `0x10` | `PROTO_ESPHOME` | Protocolo nativo ESPHome (reservado, ver §16) |
| `0x11` | `PROTO_COAP` | CoAP directo sobre L2 sin stack IP (futuro, para nodos batería ultra-ligeros) |
| `0x12` | `PROTO_MQTT_SN` | MQTT-SN sobre L2 sin stack IP (futuro) |
| `0x20`–`0xEF` | `PROTO_USER_*` | Protocolos propietarios del usuario |
| `0xF0`–`0xFF` | Reservado | Uso experimental / diagnóstico |

**Justificación del tamaño (1 byte vs 2 bytes):** EtherType usa 2 bytes para acomodar miles de protocolos registrados por IANA. En una red mesh embebida cerrada, 256 valores son más que suficientes y el ahorro de 1 byte por frame es relevante con el MTU de 250 bytes. Si en el futuro fuera necesario ampliar, el campo `Version` del header permite negociar un formato de frame extendido.

#### 4.1.7 Huella por tipo de frame (verificación de viabilidad con 250 bytes)

| Frame | Header | Payload | Tag | Total |
| ------- | -------- | --------- | ----- | ------- |
| KEY_EXCH_HELLO | 22B | pubkey(32B) + nonce_challenge(32B) = 64B | 12B | **98B** ✓ |
| KEY_EXCH_REPLY | 22B | 64B | 12B | **98B** ✓ |
| KEY_EXCH_CONFIRM | 22B | challenge(16B) | 12B | **50B** ✓ |
| DATA (payload 176B) | 22B | 176B | 12B | **210B** ✓ |
| ROUTE_ADV (18 entradas IPv4) | 22B | 18×12B=216B | 12B | **250B** ✓ |
| DATA_FRAG | 22B | frag_hdr(4B) + data(≤200B) | 12B | **≤238B** ✓ |

### 4.2 Seguridad: Dos Anillos de Cifrado

#### A. Clave de Red (Network Key)

```text
NetworkKey = HKDF-SHA256(PSK, salt="enigmang-net-v1", info="broadcast", len=16)
NetworkID  = HKDF-SHA256(PSK, salt="enigmang-net-v1", info="netid",     len=2)
```

- Usada para: JOIN_BEACON, ROUTE_ADV, ARP_QUERY, DHCP_REQUEST, CONTROL, KEY_EXCH (autenticación de mensajes planos).
- Todos los nodos miembros (misma PSK) pueden descifrar broadcasts.
- El Network ID en el header permite descartar silenciosamente frames de otras redes sin intentar descifrado.

#### B. Clave de Enlace (Link Key) — Handshake ECDH

El handshake sigue el patrón **Station-to-Station (STS)** simplificado, adaptado de TLS 1.3:

```text
Nodo A                                    Nodo B
  │─── KEY_EXCH_HELLO ──────────────────▶│
  │    pubA_efimera + nonceA              │
  │                                       │ Genera par efímero B
  │◀── KEY_EXCH_REPLY ─────────────────── │
  │    pubB_efimera + nonceB              │
  │                                       │
  │ SharedSecret = ECDH(privA, pubB)      │ SharedSecret = ECDH(privB, pubA)
  │ LinkKey = HKDF(SharedSecret,          │
  │           salt=PSK,                   │
  │           info="link"||macA||macB,    │
  │           len=16)                     │
  │                                       │
  │─── KEY_EXCH_CONFIRM ────────────────▶│
  │    AEAD{challenge=nonceA XOR nonceB}  │  (verifica LinkKey)
  │                                       │
  │◀── KEY_EXCH_CONFIRM ─────────────────│
  │    AEAD{challenge=nonceB XOR nonceA}  │  (verifica LinkKey bidireccional)
```

- **Autenticación PSK:** El salt del HKDF incluye la PSK. Un nodo con PSK incorrecta derivará un LinkKey diferente y el challenge fallará.  
- **Forward secrecy:** Los pares efímeros se destruyen tras el handshake. Una clave comprometida a posteriori no permite descifrar sesiones pasadas.
- **Protección contra replay:** Los nonces del HELLO/REPLY son aleatorios de 32 bytes (CSPRNG). El challenge usa `nonceA XOR nonceB`, garantizando que ambos contribuyen a la prueba.

### 4.3 Rotación de Clave — Decisión: Epoch + Rechazo + Renegociación

**Análisis de las dos opciones:**

| Criterio | Epoch + Overlap (v1.0) | Sin Epoch + Rechazo (propuesto) | **Epoch + Rechazo (adoptado)** |
| ---------- | ---------------------- | -------------------------------- | ------------------------------- |
| RAM extra durante rotación | 2× LinkKey por peer | 1× LinkKey | 1× LinkKey |
| Pérdida de mensajes en rotación | Ninguna | 1 mensaje rechazado | 1 mensaje rechazado |
| Complejidad de implementación | Alta (gestionar ventana de overlap) | Media | Media |
| Latencia de rotación | Transparente | ~100ms (renegociación) | ~100ms |
| Detección de epoch incorrecto | Implícita (probando ambas claves) | No hay epoch (requiere intento de descifrado fallido) | Explícita (campo epoch en header) |

**Decisión: Epoch + Rechazo + Renegociación (sin overlap)**

Justificación:

1. La rotación por defecto es cada 86.400s (24h). Una latencia de ~100ms es completamente imperceptible.
2. Sin overlap se elimina la complejidad de mantener dos claves activas por peer.
3. El campo `Epoch` (1 byte, ya en el header) permite detectar mismatch **antes** de intentar el descifrado, evitando el coste computacional de un AES-GCM fallido.
4. El mensaje rechazado se señaliza con `KEY_NACK` (nuevo frame type) que incluye el epoch esperado, lo que acelera la renegociación.

**Protocolo de rotación:**

```text
Receptor detecta epoch N+1 en frame de peer que conocía con epoch N
  │
  ├── Envía KEY_NACK {epoch_esperado: N+1}
  ├── Receptor guarda el frame rechazado en buffer (max 1 por peer)
  │
Emisor recibe KEY_NACK
  ├── Inicia KEY_EXCH_HELLO inmediatamente
  ├── Tras KEY_EXCH_CONFIRM exitoso, retransmite el frame guardado
  └── Purga el buffer
```

**Nodos batería:** Al despertar, comparan el epoch almacenado en RTC memory con el epoch del primer frame recibido. Si difieren, inician renegociación antes de enviar datos.

### 4.4 PeerManager

```cpp
struct PeerEntry {
    uint8_t  mac[6];
    uint8_t  linkKey[16];     // AES-128 = 16 bytes (no 32)
    uint8_t  epoch;
    int8_t   avgRssi;         // EWMA α=0.3
    bool     canRelay;
    bool     isBattery;       // Para gestionar ventanas RX
    uint32_t lastSeen;        // millis()
    uint32_t lastSeqRx;       // Anti-replay: última seq recibida
    uint16_t lastSeqTx;       // Seq del próximo frame a enviar
    uint16_t routeCount;      // Rutas que usan este peer como nextHop
};
// sizeof(PeerEntry) ≈ 36 bytes (vs 52 en v1.0)
```

- **Almacenamiento:** Hash table de direccionamiento abierto (más eficiente en RAM que `std::map` con su overhead de árbol rojo-negro). Tamaño inicial: 16 slots, crecimiento dinámico hasta `PEER_MANAGER_HEAP_THRESHOLD`.
- **Evicción LRU:** Si `esp_get_free_heap_size() < PEER_HEAP_LOW_WATERMARK` (default: 20KB), evictar el peer con `lastSeen` más antiguo y `routeCount == 0`.
- **Cleanup periódico:** Cada 60s, eliminar peers con `lastSeen > PEER_TIMEOUT` (default: 3600s).

> **Anti-replay:** Se rechaza cualquier frame con `seq ≤ lastSeqRx` del mismo peer. El campo Sequence (2 bytes, 65536 valores) se reinicia en cada renegociación de clave; el Epoch evita confusión entre sesiones.

---

## 5. Onboarding y Descubrimiento de Canal

### 5.1 Mecanismo Principal: AP Permanente en Gateway

**Decisión: AP permanente (no temporal) en gateways.**

Justificación:

- Los gateways están alimentados permanentemente (no hay restricción de energía).
- El ESP32 soporta modo dual AP+STA simultáneo con MACs independientes, sin penalización de rendimiento significativa para la carga ESP-NOW esperada.
- Un AP permanente simplifica el onboarding (sin esperar ventanas de 10 min) y elimina la lógica de temporizador.

**Configuración del AP de onboarding:**

```text
SSID:     ENIGMA-<NetworkID_HEX>-CH<CANAL>
Password: HMAC-SHA256(PSK, "onboarding")[:8] en hex  (16 caracteres hex)
```

> **Por qué no exponer la PSK directamente como contraseña WiFi:** Si un atacante escanea el SSID y obtiene la contraseña WiFi, obtiene la PSK y puede descifrar toda la red. Usando una derivación `HMAC(PSK, "onboarding")[:8]`, la contraseña del AP de provisioning es diferente de la clave de red, y la PSK no es directamente recuperable por fuerza bruta del hash en tiempo razonable. El nodo nuevo igualmente necesita la PSK preconfigurada para derivar las claves mesh.

**Protocolo de provisioning (HTTP sobre AP):**

```text
GET http://192.168.4.1/provision
→ {
    "network_id": "A1B2",
    "channel": 6,
    "gateway_mac": "AA:BB:CC:DD:EE:FF",
    "ip_range": "10.200.0.0/16",
    "broker": "10.200.0.1:1883"   // Dirección MQTT broker (ver §10.2)
  }
```

El nodo nuevo desconecta del AP WiFi, configura ESP-NOW en el canal indicado, y envía `JOIN_BEACON` cifrado con la Network Key.

### 5.2 Fallback: Búsqueda Ciega de Canal

Para nodos que no tienen acceso al AP de provisioning (ya configurados, que se reinician, o que están fuera del alcance del gateway):

1. El nodo busca el SSID `ENIGMA-*` en modo WiFi scan. Si lo encuentra, usa el canal codificado en el SSID.
2. Si no hay SSID visible, escanea canales 1 → 6 → 11 → resto, escuchando ESP-NOW durante `CHANNEL_DWELL_TIME` (200ms por canal).
3. Busca `JOIN_BEACON` con Magic `0x454E` y Network ID derivado de su PSK configurada.
4. Los nodos gateway y relay envían `JOIN_BEACON` cada 5 segundos en broadcast.

> **Resolución de la cuestión abierta (§5.3 v1.0):** El AP es el mecanismo primario para el onboarding inicial. La búsqueda ciega es el fallback para reconexión tras reinicio o cambio de canal, donde el nodo ya tiene la PSK y solo necesita encontrar el canal. No es un conflicto: se usan en escenarios distintos.

### 5.3 Propagación del Canal a Nodos Fuera de Alcance del Gateway

- Los nodos relay incluyen el canal actual en sus `ROUTE_ADV` (campo `channel` en flags de gateway).
- Prioridad: canal anunciado por el nodo con menor `hopCount` al gateway.

---

## 6. Protocolo de Routing (DVR)

### 6.1 Diseño: Distance Vector Routing (estilo RIPv2)

**Decisión: DVR proactivo ligero, no OLSR ni BATMAN.**

Justificación:

- **AODV (reactivo):** Introduce latencia en cada nueva comunicación (descubrimiento de ruta). Inaceptable para MQTT o TCP.
- **OLSR:** Disemina la topología completa (Link State). El overhead de broadcast en una red ESP-NOW de 50 nodos con MTU de 216 bytes sería prohibitivo.
- **BATMAN-adv:** Diseñado para interfaces Ethernet; su concepto de OGM (Originator Message) es equivalente a nuestro ROUTE_ADV pero más complejo.
- **DVR (RIPv2-like):** Simple, probado, escalable hasta ~50-100 nodos (límite práctico de una red ESP-NOW). Split Horizon + Poison Reverse son mecanismos estándar bien entendidos que evitan los problemas clásicos de DVR (count-to-infinity).

### 6.2 Route Advertisement (RA)

- **Frecuencia base:** `RA_INTERVAL` = 30s.
- **RA disparado:** Cuando se detecta un cambio de topología (peer nuevo, peer perdido, ruta mejor), se envía un RA inmediato (triggered update), luego se reinicia el timer.
- **Contenido por entrada IPv4:**

  ```text
  [IPv4: 4B][MAC_destino: 6B][HopCount: 1B][Flags: 1B] = 12 bytes/entrada
  ```

  El `nextHop` no se incluye en la RA: **es el emisor del RA**. Esto ahorra 6 bytes por entrada.

- Con overhead de 34B: disponibles 216B → **18 entradas por frame** (IPv4, 216/12=18 exacto). El frame ROUTE_ADV completo ocupa exactamente 250 bytes.
- Si la tabla supera 18 entradas, se fragmenta en múltiples RA consecutivos con el flag `RA_CONTINUATION`.

### 6.3 Tabla de Routing / ARP Unificada

```cpp
struct RouteEntry {
    uint32_t ip;              // IPv4 (4B). Para v1.x: uint8_t ip[16]
    uint8_t  mac[6];          // MAC del destino final
    uint8_t  nextHop[6];      // MAC del siguiente salto
    uint8_t  hopCount;        // 0=local, 255=infinito (Poison Reverse)
    int8_t   rssi;            // RSSI al nextHop
    uint32_t lastUpdate;      // millis()
    uint16_t ttl;             // Segundos hasta expirar
    uint8_t  flags;           // IS_GATEWAY | IS_BATTERY | IS_DIRECT
};
// sizeof(RouteEntry) = 4+6+6+1+1+4+2+1 = 25 bytes
// Pool de 64 entradas = 1.600 bytes
```

**Control de memoria:**

- Pool estático de `MAX_ROUTES` (default 64, configurable en compilación).
- Evicción cuando tabla llena: 1) entradas expiradas, 2) mayor hopCount, 3) menos recientemente actualizada.

**TTL de rutas:**

| Tipo | TTL por defecto | Justificación |
| ------ | ---------------- | --------------- |
| Vecino directo | 90s | Pérdida rápida si el vecino desaparece |
| 2–4 hops | 180s | Convergencia en 2 intervalos RA |
| 5+ hops | 300s | Red probablemente estable a distancia |

> **Resolución de la cuestión abierta (TTL 300s):** El TTL no es el mecanismo primario de convergencia; lo son los triggered updates y ROUTE_WITHDRAW. Los 300s son un seguro contra pérdida de withdraws. Para redes móviles, reducir a 90s y `RA_INTERVAL` a 15s. Definir con `MESH_MOBILE_MODE` como preset de compilación.

### 6.4 Prevención de Bucles — Seen-Frame Cache

**Decisión: eliminar el campo `Path` del header y usar una caché de frames vistos.**

Justificación:

- El campo `Path` con 8 MACs costaba hasta **49 bytes** por frame (1B `PathLen` + 6B×8).
- Una caché de 32 entradas `(SrcMAC[6], Seq[2])` ocupa solo **256 bytes de RAM** por nodo relay.
- Este es el mecanismo estándar de loop prevention en 802.11s (WiFi Mesh) y BATMAN-adv.
- El TTL de IP (decrementado por lwIP) sigue siendo la barrera de seguridad final.

```cpp
struct SeenFrame {
    uint8_t  srcMac[6];
    uint16_t seq;
    uint32_t timestamp;   // Para expirar entradas
};
// Buffer circular de 32 entradas = 32 × 12B = 384 bytes por nodo relay
```

Un relay descarta el frame si `(srcMac, seq)` está en la caché (recibido en los últimos `SEEN_FRAME_TTL` = 10s). Si no está, lo añade y lo reenvía.

**Mecanismos adicionales (estándar DVR):**

1. **Split Horizon:** No se anuncia en RA hacia peer X las rutas cuyo `nextHop == X`.
2. **Poison Reverse:** Rutas aprendidas de X se anuncian a X con `hopCount = 255`.
3. **TTL IP:** lwIP decrementa TTL; paquetes con TTL=0 se descartan.

### 6.5 Route Withdraw

#### Timeouts por tipo de nodo

| Tipo | Timeout | Razón |
|---|---|---|
| Normal | 90s (`3 × RA_INTERVAL`) | 3 ROUTE_ADV consecutivos perdidos |
| Batería | `max(3 × sleepInterval + 60s, 120s)` | El nodo puede estar en deep sleep legítimamente |

El `sleepInterval` lo anuncia el propio nodo batería en el campo extra del `JOIN_BEACON`:

```
[channel: 1B][localIP: 4B][mode: 1B][sleepIntervalSec: 4B — solo si mode==MESH_BATTERY]
```

Los vecinos leen este campo y lo almacenan en `PeerEntry.sleepIntervalMs` para calcular su propio timeout dinámico.

#### Comportamiento al detectar caída

Cuando `_checkPeerTimeouts()` expira un peer:
1. Elimina rutas locales (`handleRouteWithdraw(mac)`).
2. Invoca el callback `onNodeLeave`.
3. **Emite `ROUTE_WITHDRAW` broadcast** (payload = 6 bytes MAC del peer caído), permitiendo que todos los vecinos directos converjan en < 1s en lugar de esperar hasta 90s.

#### Recepción de ROUTE_WITHDRAW

Al recibir un `ROUTE_WITHDRAW`:
1. MAC del payload == MAC propia → ignorar.
2. Sin rutas hacia esa MAC → ignorar (ya convergió; no se retransmite para evitar tormentas).
3. Con rutas → eliminarlas y disparar `onNodeLeave` si era peer directo.

> **Convergencia multi-hop:** El `ROUTE_WITHDRAW` alcanza a los vecinos directos del nodo que detectó la caída. Los nodos más lejanos convergen por expiración de ruta (máx. 90s adicionales). Para redes > 3 hops se puede habilitar retransmisión controlada en una futura versión.

---

## 7. Nodos Batería y Ventanas de Recepción (LoRaWAN-style)

### 7.1 Ciclo de Vida (Clase A, como LoRaWAN)

```text
[DEEP SLEEP] ──(T_sleep)──▶ [WAKE] ──▶ [TX UPLINK] ──▶ [RX1: 2s] ──▶ [RX2: 2s] ──▶ [SLEEP]
```

El modelo LoRaWAN Clase A es el óptimo para nodos IoT de batería: el nodo solo escucha inmediatamente después de transmitir. Sin polling activo, sin radio encendida en espera.

### 7.2 Parent Node

- El nodo batería elige su Parent durante el join: el vecino relay con mejor `avgRssi`.
- El Parent almacena un buffer FIFO de downlink por hijo: máximo 5 mensajes, máximo 200 bytes cada uno.
- Al recibir el UPLINK, el Parent vacía el buffer en ventanas RX1 + RX2.

### 7.3 Heartbeat

- `BATTERY_HEARTBEAT_INTERVAL` (default: 1h).
- Si el Parent no recibe heartbeat en `3 × HEARTBEAT_INTERVAL`, libera el buffer y retira las rutas del hijo.

### 7.4 Sincronización de Reloj

El Parent incluye un timestamp (segundos UTC aproximados) en la respuesta al UPLINK. El nodo batería almacena el offset en `RTC_DATA_ATTR` (RTC memory, persiste en deep sleep).

Uso:

- Wake-up precisos con `esp_sleep_enable_timer_wakeup()`.
- Verificación de expiración de epoch de clave.
- Timestamps en telemetría.

### 7.5 Redescubrimiento de Parent — Resolución de la Cuestión Abierta

**Estrategia energéticamente eficiente:**

El nodo batería almacena en NVS una lista ordenada de hasta **3 candidatos a Parent** (MAC + última `avgRssi`), actualizada en cada UPLINK exitoso.

Al despertar:

1. Intenta UPLINK al Parent primario (MAC almacenada). Timeout: 4s (RX1 + RX2).
2. Si no hay ACK, prueba el segundo candidato. Timeout: 4s.
3. Si no hay ACK, prueba el tercero. Timeout: 4s.
4. Si los tres fallan, inicia búsqueda ciega de canal (§5.2) y re-join completo.

**Justificación:** El re-join completo (búsqueda ciega + ECDH handshake) consume ~100-500ms de radio activa. Mantener 3 candidatos en NVS (coste mínimo: 20 bytes) permite recuperar movilidad sin re-join en la mayoría de casos.

---

## 8. Capa IP (IPv4) y MTU

### 8.1 Interfaz Virtual (netif)

- `esp_netif_new()` con driver personalizado de tipo `ESP_NETIF_ID_CUSTOM`.
- **Input path:** Frame `DATA` descifrado → `esp_netif_receive()`.
- **Output path:** `mesh_netif_output()` → cifrar → `sendUnicast/sendBroadcast()`.

### 8.2 MTU y TCP — Análisis y Decisión

**Cálculo del MTU efectivo:**

```text
ESP-NOW payload:  250 bytes
Header L2:        22 bytes   (incluye 1B Protocol)
Tag GCM:          12 bytes
────────────────────────────
Payload IP:       216 bytes  →  MTU = 216 bytes
```

**Fragmentación L2 vs IP — Resolución del punto abierto:**

| Opción | Pros | Contras |
| -------- | ------ | --------- |
| Fragmentación IP (en lwIP) | Transparente para la app; standard RFC 791 | Requiere reensamblaje en el destino final; los fragments intermedios pueden tomar rutas diferentes, complicando el multi-hop |
| Fragmentación L2 (en la capa mesh) | Reensamblaje siempre en el siguiente hop; más predecible | Overhead de 4B extra por fragment; no transparente a lwIP |
| **MTU grande (ninguna fragmentación para tráfico típico IoT)** | Cero overhead de fragmentación | Payloads > 216B requieren L2 fragmentation |

**Decisión:**

- Fijar `netif MTU = 216 bytes`. lwIP usará esto para ajustar MSS automáticamente.
- Configurar `TCP_DEFAULT_MSS = 176` en `lwipopts.h` (`216 - 20 IP - 20 TCP = 176`). Los segmentos TCP caben en un solo frame ESP-NOW sin fragmentación L2.
- Para UDP: payloads ≤ 188 bytes (216 - 20 IP - 8 UDP) no necesitan fragmentación. MQTT over UDP o CoAP son los protocolos recomendados.
- Para `PROTO_ESPHOME` u otros protocolos no-IP (§4.1.6): el payload disponible es directamente los 216 bytes sin overhead de cabeceras IP/TCP, lo que los hace más eficientes para mensajes cortos.
- La fragmentación L2 sigue disponible para payloads IP excepcionales (> 216B), con header de 4B y timeout de reensamblaje de 2s.

**Configuración lwIP recomendada en `lwipopts.h`:**

```c
#define TCP_MSS              176
#define TCP_WND              (4 * TCP_MSS)   // 704 bytes = 4 segmentos en vuelo
#define TCP_SND_BUF          (4 * TCP_MSS)
#define LWIP_TCP_SACK_OUT    0               // Desactivar SACK, ahorra RAM
```

Una ventana TCP de 4 segmentos es adecuada para la latencia típica de una red ESP-NOW multi-hop (~50ms RTT por hop). Ventanas mayores desperdiciarían RAM sin beneficio de throughput.

### 8.3 Asignación de Direcciones IP

Se soportan tres modos, seleccionables en `begin()`:

| Modo | Escenario | Comportamiento |
| ------ | --------- | --------------- |
| **Estático distribuido** (default) | Nodos batería, instalaciones fijas | MAC→IP almacenada en NVS; tabla distribuida en ROUTE_ADV como campo opcional |
| **DHCP** | Nodos conectados (no batería) con IP dinámica | Servidor DHCP en gateway via lwIP dhcpserver |
| **IP fija manual** | Configuración explícita del usuario | `begin(psk, IPAddress(10,200,0,x))` |

**Subred:** `10.200.0.0/16` (configurable). El gateway hace routing hacia la LAN WiFi y NAT hacia Internet (ver §9).

### 8.4 ARP

- La tabla de routing unificada resuelve IP→MAC localmente sin tráfico.
- `ARP_QUERY` broadcast solo si la IP no está en tabla.
- Los nodos envían `ARP_REPLY` gratuitous en el join (anuncian su IP→MAC), minimizando queries.

### 8.5 ICMP y Diagnóstico de Red (Ping)

**ICMP funciona de forma completamente transparente sin ningún cambio en la capa mesh.**

Justificación:

- ICMP es el protocolo IP número 1 (campo `Protocol` de la cabecera IPv4, distinto del campo `Protocol` del header mesh). Un paquete ICMP echo request/reply es simplemente un paquete IPv4 con `PROTO_IPV4 (0x01)` en el frame EnigmaNG.
- lwIP maneja ICMP internamente para todo netif registrado. Al llegar un frame `DATA` con `PROTO_IPV4`, se inyecta en `mesh0` via `esp_netif_receive()` y lwIP responde automáticamente al echo request sin intervención de la aplicación.
- Los nodos relay simplemente reenvían el frame como cualquier otro DATA. No tienen visibilidad del protocolo IP interior.

**Tamaño de un frame ping típico:**

```
ICMP echo request: 20B IP + 8B ICMP header + 32B datos = 60B payload
Frame total:       22B header + 60B + 12B tag = 94 bytes  ✓  (muy por debajo de 250B)
```

**API `esp_ping` de IDF — uso transparente:**

```c
#include "ping/ping_sock.h"

// Ping desde un nodo mesh a otro nodo mesh (o a través del gateway)
esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
config.target_addr.u_addr.ip4.addr = ipaddr_addr("10.200.0.5");
config.target_addr.type = IPADDR_TYPE_V4;
// Sin especificar interfaz: lwIP usa la tabla de rutas → selecciona mesh0
// automáticamente para IPs en 10.200.0.0/16
esp_ping_callbacks_t cbs = { .on_ping_success = on_success, ... };
esp_ping_handle_t ping;
esp_ping_new_session(&config, &cbs, &ping);
esp_ping_start(ping);
```

> Si el nodo tiene múltiples netifs (ej. gateway con `mesh0` y `wifi_sta`), lwIP selecciona la interfaz correcta por tabla de rutas automáticamente. Se puede forzar una interfaz específica con `config.interface = mesh_netif_handle` si es necesario.

**Casos de uso de diagnóstico:**

- `ping <ip_mesh>` desde un nodo mesh: verifica conectividad end-to-end y mide latencia multi-hop.
- `ping <ip_mesh>` desde un dispositivo WiFi de la LAN (a través del gateway con routing LAN, §9.2): verifica que el routing gateway funciona correctamente.
- `ping 8.8.8.8` desde un nodo mesh: verifica el path completo mesh → gateway → NAT → Internet.
- Medir RTT por hop: hacer ping al nextHop directo da la latencia del enlace ESP-NOW (~2–5ms típico).

---

## 9. Bridge WiFi / Gateway

### 9.1 Función

- WiFi STA (a AP externo) + ESP-NOW mesh simultáneos.
- AP de onboarding permanente (segunda MAC del ESP32).
- Servidor web + Prometheus + DHCP.
- Puede haber múltiples gateways por redundancia.

### 9.2 Routing Gateway: Híbrido LAN+NAT — Resolución del Punto Abierto

La arquitectura NAT pura es incorrecta para el caso de uso: impide que dispositivos WiFi de la LAN inicien conexiones hacia nodos mesh (ej. un servidor Home Assistant accediendo a un sensor mesh).

**Decisión: Routing LAN + NAT Internet (política de routing)**

```text
Tráfico mesh → LAN WiFi (10.200.x.x → 192.168.1.x):
    → Routing directo. El gateway actúa como router.
    → Requiere que el router WiFi tenga una ruta estática:
      "10.200.0.0/16 via <IP del gateway>"
    → El gateway anuncia esto via mDNS (TXT record) y en la Web UI.

Tráfico mesh → Internet (10.200.x.x → 0.0.0.0/0):
    → NAT masquerading con la IP WiFi del gateway.
    → Usar ip_napt de lwIP si disponible en IDF 5.5.4; si no,
      implementación custom via raw socket.
```

**Implementación:**

- El gateway mantiene dos interfaces: `mesh0` (netif mesh) y `wifi_sta` (netif WiFi).
- lwIP ip_forward habilitado entre ambas interfaces.
- Regla NAT: si `dst_ip` no está en ninguna subred local conocida, aplicar masquerade.

**Para dispositivos WiFi que no configuran ruta estática:** El gateway puede responder ARP por la subred mesh (proxy ARP), haciendo transparente el routing. Esto es opcional y configurable (`GATEWAY_PROXY_ARP`).

### 9.3 Selección de Gateway

Los gateways anuncian en `ROUTE_ADV` el flag `IS_GATEWAY` y su métrica:

```text
Metric = hopCount × 100 + (100 + wifi_rssi)   // wifi_rssi es negativo
```

Los nodos eligen el gateway con menor métrica. Criterio de desempate: menor `hopCount`, luego mejor RSSI al nextHop.

### 9.4 Redundancia

- Si un gateway desaparece, sus rutas expiran y los nodos migran automáticamente.
- Las conexiones TCP activas se rompen al cambiar de gateway (diferente IP NAT). Aceptable para v1.0.

### 9.5 Gateway Avanzado: Dual-Board con ESP-Hosted (Opción Avanzada)

**Opción siempre disponible: single-chip (§9.1). Esta sección describe una variante avanzada que no reemplaza ni modifica la anterior.**

> ⚠️ **Requisito de entorno: IDF nativo obligatorio.** ESP-Hosted-MCU es un componente ESP-IDF puro (instalado via `idf.py add-dependency`). **No es compatible con Arduino Core ESP32**, que aunque internamente usa IDF 5.5.4, no expone el sistema de componentes IDF de forma utilizable. El gateway dual-board debe desarrollarse como un **proyecto CMake nativo de ESP-IDF**, fuera del framework Arduino. Esto no afecta a los demás nodos de la mesh, que siguen usando Arduino Core.

#### 9.5.1 Motivación: eliminación de la restricción de canal

El principal problema del gateway single-chip es la restricción de canal (§3.2): WiFi y ESP-NOW comparten la misma radio, por lo que la mesh debe operar en el mismo canal que el AP WiFi al que el gateway está conectado. Esto impide elegir libremente el canal mesh, complica el roaming y puede degradar el rendimiento ESP-NOW en canales congestionados.

Con un gateway dual-board:

- **Placa mesh (master):** ESP32 ejecutando EnigmaNG completo como **proyecto IDF nativo**. Gestiona ESP-NOW en el canal óptimo para la mesh (libre elección).
- **Placa WiFi (slave/co-procesador):** ESP32/ESP32-S3/ESP32-C6 ejecutando el firmware slave de [ESP-Hosted-MCU](https://github.com/espressif/esp-hosted-mcu). Gestiona la conexión WiFi STA al AP externo + el AP de onboarding, en su propio canal WiFi, completamente independiente.

```text
┌─────────────────────────┐       SPI/UART      ┌──────────────────────────┐
│  Placa Mesh (Master)    │◄───────────────────►│  Placa WiFi (Slave)      │
│  ESP32 — EnigmaNG       │                     │  ESP32/S3/C6             │
│                         │                     │  ESP-Hosted-FG firmware  │
│  netif: mesh0           │                     │                          │
│  netif: wifi_sta        │  ← driver hosted →  │  WiFi STA (cualq. canal) │
│          (via Hosted)   │                     │  AP onboarding           │
│  routing/NAT/web/DHCP   │                     │                          │
└─────────────────────────┘                     └──────────────────────────┘
         ▲ ESP-NOW mesh (canal libre)
```

#### 9.5.2 Entorno de desarrollo y compatibilidad con Arduino

**Por qué ESP-Hosted-MCU no es compatible con Arduino Core:**

| Aspecto | Arduino Core ESP32 | IDF nativo |
| -------- | ------------------- | ------------ |
| Sistema de build | Arduino IDE / PlatformIO Arduino framework | CMake + `idf.py` |
| Gestión de componentes | Librerías `.zip` / `library.json` | ESP Component Manager (`idf_component.yml`) |
| ESP-Hosted-MCU | ❌ No soportado | ✅ `idf.py add-dependency "espressif/esp_hosted^2.12.6"` |
| `esp_wifi_remote` | ❌ No disponible | ✅ Parte del componente |
| Versión IDF requerida | IDF 5.5.4 (interno, no accesible) | IDF 5.3+ explícito |

Arduino Core ESP32 3.3.8 usa IDF 5.5.4 internamente, pero lo encapsula de forma que el componente ESP-Hosted-MCU no puede integrarse: el sistema de componentes de IDF requiere acceso al CMake build system y al `idf_component.yml` del proyecto, que no existen en un proyecto Arduino.

**Implicación práctica:** El gateway dual-board es un **proyecto IDF separado**. EnigmaNG provee en este caso una librería IDF (no Arduino) con el mismo código de capa mesh y routing, compilada como componente CMake. La abstracción `MeshUplink` permite reutilizar toda la lógica de gateway sin duplicar código.

**Estructura del repositorio resultante:**

```text
EnigmaNG/
  src/                    ← Código principal (IDF + compatible con Arduino)
  arduino/                ← Wrapper Arduino: MeshNetwork.h/.cpp
  idf_component/          ← CMakeLists.txt + idf_component.yml
  examples/
    arduino/              ← Ejemplos Arduino (nodos, gateway single-chip)
    idf/
      gateway_hosted/     ← Gateway dual-board (IDF nativo)
```

#### 9.5.3 Impacto en el código EnigmaNG

Desde el punto de vista del gateway EnigmaNG, el cambio en la lógica es **mínimo e intencionado**:

- El netif `wifi_sta` se inicializa con el driver `esp_hosted` en lugar del driver `esp_wifi` nativo.
- El netif `mesh0` se inicializa igual que en single-chip.
- El resto del código (routing LAN+NAT §9.2, selección de gateway §9.3, web UI §11) es **idéntico**.
- Se introduce una abstracción `MeshUplink` con dos implementaciones: `NativeWifiUplink` y `HostedWifiUplink`.

```cpp
class MeshUplink {
public:
    virtual bool begin(const char* ssid, const char* pass) = 0;
    virtual esp_netif_t* getNetif() = 0;
    virtual int8_t getRssi() = 0;
    virtual bool isConnected() = 0;
};

class NativeWifiUplink  : public MeshUplink { /* esp_wifi nativo */ };
class HostedWifiUplink  : public MeshUplink { /* esp_hosted driver */ };
```

El gateway instancia la implementación correcta en función de si `ENIGMANG_HOSTED_UPLINK` está definido en tiempo de compilación. No hay diferencia en la lógica de gateway.

**Dependencias del proyecto IDF gateway_hosted:**

```yaml
# idf_component.yml
dependencies:
  espressif/esp_hosted: "^2.12.6"    # Componente ESP-Hosted-MCU
  espressif/esp_wifi_remote: "*"     # Interfaz API WiFi para hosted
  idf: ">=5.3"
```

#### 9.5.4 Interfaz física entre placas

Datos reales de throughput de la documentación oficial de ESP-Hosted-MCU (medidos over-the-air):

| Interfaz | Modo | GPIOs | UDP Mbps | TCP Mbps | Notas |
| ---------- | ------ | ------- | ---------- | ---------- | ------- |
| **SPI estándar FD** | Full duplex | 6 | 24 | 22 | Recomendado para prototipo; cables jumper |
| **UART** | Full duplex | 2 | 0.68 | 0.60 | Suficiente para mesh; baud 921.600 |
| SDIO 4-bit | Half duplex | 6 | 68.1 | 44.0 | PCB obligatorio; overkill para IoT mesh |

Para EnigmaNG: **SPI estándar** durante desarrollo/prototipo, **UART** en producción si el throughput es suficiente (lo es: el throughput agregado de una red ESP-NOW raramente supera 500 kbps efectivos). UART tiene la ventaja de usar solo 2 cables de datos más reset.

> **Nota sobre UART:** La documentación de ESP-Hosted indica 0.68 Mbps con baud 921.600. En ESP32-P4+C6 se alcanzan 3.3 Mbps con 4 Mbps baud rate, pero ESP32 clásico como master no soporta ese baud. Para ESP32, UART a 921.600 = ~680 kbps — suficiente.

#### 9.5.5 Opciones de chip para la placa WiFi slave

| Chip | Ventaja | Nota |
| ------ | --------- | ------ |
| ESP32 (clásico) | Más barato, bien probado con ESP-Hosted | Primera opción para prototipos |
| ESP32-S3 | Más RAM/flash, dual-core | Sin ventaja real para este rol |
| **ESP32-C6** | WiFi 6 (802.11ax), mejor eficiencia espectral | Recomendado para producción; también tiene Zigbee/Thread |

#### 9.5.6 AP de Onboarding en modo dual-board

En modo dual-board, el AP de onboarding puede correr en la placa WiFi slave (que ya tiene radio WiFi activa). El slave lo levanta automáticamente como segundo SSID (STA + SoftAP simultáneo, estándar en ESP32). El master recupera el resultado del provisioning a través de la interfaz SPI/UART como si fuera una respuesta HTTP local.

Alternativamente, el master puede levantar su propio AP de onboarding como en single-chip, usando únicamente la interfaz ESP-NOW. En este caso, el AP de onboarding queda en la placa slave y el protocolo de provisioning (§5.1) es transparente al master.

---

## 10. Soporte ESP8266 (Librería Derivada)

### 10.1 Limitaciones — Decisión: Proxy MQTT, no netif virtual

Implementar un `netif` virtual en lwIP para ESP8266 bajo Arduino requiere parchar el core de Arduino para ESP8266, lo que introduce una dependencia de mantenimiento inaceptable. El enfoque proxy es más pragmático y cubre el 95% de los casos de uso reales (publicar/suscribirse a MQTT).

**Limitaciones aceptadas:**

- No relay (el ESP8266 no puede reenviar frames para otros nodos).
- No stack IP propio.
- No bridge.

### 10.2 Arquitectura: Proxy MQTT — Resoluciones de Puntos Abiertos

#### Dirección del broker — Decisión: configurada en gateway, distribuida por beacon

La dirección del broker se incluye en la respuesta del AP de provisioning (§5.1) y en el `JOIN_BEACON`:

```json
{
  "broker": "192.168.1.100:1883",
  "broker_user": "",
  "broker_pass": ""
}
```

**Justificación:** Centralizar la configuración del broker en el gateway es más mantenible. Si el broker cambia de IP, solo hay que actualizar el gateway; los nodos ESP8266 reciben la nueva dirección en el próximo provisioning o en el `JOIN_BEACON`. La opción de configurarlo en el nodo ESP8266 es más transparente pero obliga a reflashear todos los nodos ante un cambio de broker.

**Compromiso:** El ESP8266 almacena la dirección del broker en EEPROM tras el primer provisioning, y la actualiza si recibe un `JOIN_BEACON` con una dirección diferente.

#### Confirmación de entrega — Decisión: sin ACK adicional

ESP-NOW confirma la entrega peer-to-peer a nivel de hardware (callback `OnDataSent` con estado). El enlace ESP8266↔ESP32 proxy es siempre de 1 hop directo, por lo que la confirmación ESP-NOW es suficiente para garantizar que el proxy recibió el mensaje.

Para mensajes que requieren confirmación end-to-end (MQTT QoS 1/2), el broker MQTT ya proporciona el PUBACK. El proxy puede reenviar el PUBACK al ESP8266 vía `PROXY_ACK`.

#### Protocolo Proxy

```cpp
enum ProxyMsgType : uint8_t {
    PROXY_CONNECT      = 0x01,
    PROXY_PUBLISH      = 0x02,
    PROXY_SUBSCRIBE    = 0x03,
    PROXY_UNSUBSCRIBE  = 0x04,
    PROXY_MESSAGE      = 0x05,  // Mensaje del broker al ESP8266
    PROXY_PUBACK       = 0x06,  // MQTT QoS1 confirmación
    PROXY_DISCONNECT   = 0x07,
    PROXY_DISCOVERY    = 0x08,  // Broadcast: buscar proxy disponible
    PROXY_OFFER        = 0x09   // Respuesta al discovery
};
```

El ESP8266 envía `PROXY_DISCOVERY` broadcast. Los ESP32 vecinos responden `PROXY_OFFER` con su MAC y RSSI. El ESP8266 elige el de mejor RSSI y hace `PROXY_CONNECT`.

### 10.3 API para ESP8266

```cpp
class MeshNode8266 {
public:
    bool begin(const char* psk);
    bool mqttPublish(const char* topic, const uint8_t* payload, 
                     size_t len, uint8_t qos = 0, bool retain = false);
    bool mqttSubscribe(const char* topic, uint8_t qos = 0);
    bool mqttUnsubscribe(const char* topic);
    void onMqttMessage(MqttCallback cb);
    void onConnected(ConnectCallback cb);
    void setSleepDuration(uint32_t seconds);
    bool isConnected();
    void loop();
};
```

---

## 11. Web UI y Métricas Prometheus (Gateway)

### 11.1 Servidor Web

Usar `esp_http_server` (IDF nativo). Descartada AsyncWebServer por compatibilidad incierta con IDF 5.5.4 y dependencias adicionales.

**Endpoints:**

| Método | Path | Descripción |
| -------- | ------ | ------------- |
| GET | `/` | Dashboard HTML (topología, nodos, rutas) |
| GET | `/api/v1/status` | JSON: estado general |
| GET | `/api/v1/nodes` | JSON: lista de nodos |
| GET | `/api/v1/routes` | JSON: tabla de rutas |
| GET | `/metrics` | Prometheus text format |
| POST | `/api/v1/config` | Cambio de configuración (requiere auth) |
| GET | `/api/v1/peers` | JSON: tabla de peers y RSSI |

### 11.2 Autenticación Web — Decisión: HTTP Digest Auth

**Análisis:**

| Opción | Seguridad | Complejidad | Notas |
| -------- | ----------- | ------------- | ------- |
| Basic Auth HTTP | Baja (password en base64 claro) | Mínima | Inaceptable sin HTTPS |
| Basic Auth + HTTPS | Buena | Alta (gestión certificados TLS en ESP32) | Viable pero complejo |
| **HTTP Digest Auth** | Media-alta (no expone password) | Baja-media | RFC 7616, soportado en esp_http_server |
| JWT | Alta | Alta | Excesivo para v1.0 |

**Decisión: HTTP Digest Auth (RFC 7616)**

Justificación: Digest Auth no transmite la contraseña en claro (usa hash MD5/SHA256 del challenge). Es compatible con todos los navegadores y clientes HTTP sin configuración adicional. `esp_http_server` tiene soporte nativo para digest auth en IDF 5.x. El endpoint `/metrics` puede opcionalmente no requerir auth (Prometheus típicamente accede desde red interna).

### 11.3 Métricas Prometheus

```text
mesh_nodes_total{network="XXXX"} 15
mesh_routes_total{network="XXXX"} 32
mesh_link_rssi{src="AABBCCDDEEFF",dst="112233445566"} -72
mesh_key_epoch{link="AABBCCDDEEFF_112233445566"} 42
mesh_uptime_seconds{node="AABBCCDDEEFF"} 86400
mesh_battery_voltage{node="AABBCCDDEEFF"} 3.65
mesh_packets_total{node="AABBCCDDEEFF",direction="rx"} 15000
mesh_heap_free{node="AABBCCDDEEFF"} 45312
mesh_route_convergence_ms{network="XXXX"} 250
```

---

## 12. Descubrimiento de Servicios

### 12.1 Decisión: Protocolo Propio Ligero + mDNS estándar en interfaz WiFi del gateway

**Descartado mDNS estándar sobre la mesh:**

- Un registro mDNS típico (SRV + TXT + A) ocupa 200-400 bytes sin comprimir.
- Sobre una mesh con MTU de 216 bytes requeriría 2-3 fragmentos L2 por anuncio.
- mDNS usa multicast UDP que genera tráfico frecuente (TTL cortos, re-anuncios).

**Protocolo propio sobre mesh:**

Los registros de servicios se incluyen como campo opcional en `ROUTE_ADV`:

```text
[Service Type: 1B][Port: 2B][Name: 0-15B null-terminated]
// Ejemplo: 0x01 (HTTP), port=80, name="gateway"
// 18 bytes por registro de servicio
```

Tipos de servicio:

| Valor | Tipo |
| ------- | ------ |
| 0x01 | HTTP |
| 0x02 | MQTT broker |
| 0x03 | CoAP |
| 0x04 | Prometheus metrics |

Resolución: consulta `SERVICE_QUERY` broadcast → respuesta `SERVICE_REPLY` unicast con IP + puerto.

**mDNS estándar en el gateway:** El gateway republica los servicios mesh hacia la red WiFi vía mDNS estándar (lwIP mDNS), permitiendo que dispositivos WiFi descubran servicios mesh sin configuración.

---

## 13. Autenticación con Certificados

**Decisión: Pospuesto a v1.1.**

El modo PSK con ECDH Curve25519 proporciona seguridad suficiente para la gran mayoría de despliegues IoT. El modo CERT añade complejidad de gestión de PKI (generar, distribuir, revocar certificados) que está fuera del alcance de v1.0. Se reserva la extensión del protocolo de handshake para soportarlo en el futuro sin cambios de formato de frame.

---

## 14. API Pública de la Librería

```cpp
class MeshNetwork {
public:
    // Inicialización
    bool begin(const char* psk, MeshMode mode = MESH_NODE);
    bool begin(const char* psk, IPAddress staticIP, MeshMode mode = MESH_NODE);

    // Configuración
    void setRelayEnabled(bool enabled);
    void setBatteryMode(bool enabled, uint32_t sleepIntervalSec);
    void setRssiThreshold(int8_t connectDbm, int8_t disconnectDbm);
    void setKeyRotationInterval(uint32_t seconds);
    void setMaxRoutes(uint16_t max);

    // Estado
    bool    isConnected();
    bool    isGateway();
    int     getNodeCount();
    int8_t  getRssiTo(const uint8_t* mac);
    int8_t  getRssiFromGateway();
    IPAddress getLocalIP();

    // Integración IP transparente
    // Devuelve un Client& compatible con PubSubClient, HTTPClient, etc.
    // El stack lwIP se encarga del routing sobre la mesh.
    WiFiClient& getClient();    // Wrapper sobre socket lwIP en netif mesh0

    // Callbacks
    void onNodeJoin(MeshNodeCallback cb);
    void onNodeLeave(MeshNodeCallback cb);

    // Gateway-only
    bool startWebServer(uint16_t port = 80);
    bool startPrometheus(uint16_t port = 9090);
    void setMqttBroker(const char* host, uint16_t port);
    bool setStaticIPTable(const std::vector<std::pair<String, IPAddress>>& table);

    // Sincronización de tiempo
    time_t getMeshTime();
    void   onTimeSync(MeshTimeCallback cb);

    // Control
    void loop();
    void shutdown();
};

enum MeshMode : uint8_t {
    MESH_NODE    = 0,   // Nodo estándar con relay
    MESH_GATEWAY = 1,   // Con WiFi uplink + AP provisioning
    MESH_BATTERY = 2    // Sin relay, deep sleep cíclico
};
```

---

## 15. Plan de Desarrollo Incremental

### Fase 0: Infraestructura (1 semana)

- [ ] Setup PlatformIO con Core ESP32 3.3.8 + QuickESPNow.
- [ ] Verificar compatibilidad QuickESPNow con IDF 5.5.4.
- [ ] Test ESP-NOW básico: envío/recepción, RSSI, cambio de canal.
- [ ] Estructura de directorios: `src/`, `examples/`, `test/`.

### Fase 1: Capa Física + Frame (1 semana)

- [ ] Implementar `MeshPhysicalLayer` (wrapper QuickESPNow).
- [ ] Serializar/deserializar el header de 21 bytes.
- [ ] EWMA de RSSI con α=0.3.
- [ ] Test: 3 nodos, filtrado por Network ID.

### Fase 2: Seguridad — Handshake y Cifrado (2 semanas)

- [ ] Integrar Curve25519 + HKDF + AES-128-GCM via mbedTLS.
- [ ] Handshake HELLO → REPLY → CONFIRM con challenge.
- [ ] Nonce derivado de header (no transmitido).
- [ ] Test: 2 nodos negocian clave; nodo con PSK incorrecta es rechazado.

### Fase 3: Rotación de Clave + PeerManager (1 semana)

- [ ] Epoch + KEY_NACK + renegociación con retransmisión del frame rechazado.
- [ ] PeerManager con hash table abierto y evicción LRU.
- [ ] Anti-replay por `(peer, seq)`.
- [ ] Test: forzar rotación, verificar reconexión transparente.

### Fase 4: Routing Básico (2 semanas)

- [ ] ROUTE_ADV, tabla de routing, split horizon, poison reverse.
- [ ] Seen-Frame Cache para relay.
- [ ] Test: 3 nodos en línea A–B–C. A ping C vía B.

### Fase 5: Routing Multi-hop y Withdraw (1 semana)

- [ ] Tabla con pool estático y evicción.
- [x] ROUTE_WITHDRAW y triggered updates.
- [ ] Test: 5 nodos, desconectar nodo central, verificar reconvergencia (< 60s).

### Fase 6: Interfaz IP Virtual (netif) (2 semanas)

- [ ] `esp_netif_new()` con driver mesh.
- [ ] RX: frame DATA → `esp_netif_receive()`.
- [ ] TX: `mesh_netif_output()` → cifrar → send.
- [ ] Test: `ping` entre dos nodos con IPs estáticas.

### Fase 7: MTU, Fragmentación L2 y ARP (1 semana)

- [ ] MTU = 216, TCP MSS = 176 en `lwipopts.h`.
- [ ] Fragmentación L2 para payloads > 216 bytes.
- [ ] ARP gratuitous en join; ARP_QUERY/REPLY.
- [ ] Test: UDP de 500 bytes (requiere fragmentación). TCP MQTT funcional.

### Fase 8: DHCP + Tabla Estática (1 semana)

- [ ] Tabla estática distribuida via ROUTE_ADV.
- [ ] DHCP servidor en gateway (lwIP dhcpserver).
- [ ] Test: nodo obtiene IP; nodo batería recupera IP de NVS.

### Fase 9: Gateway + Routing LAN + NAT (2 semanas)

- [ ] Modo dual WiFi STA + AP provisioning permanente.
- [ ] ip_forward entre `mesh0` y `wifi_sta`.
- [ ] NAT (ip_napt o custom) para tráfico Internet.
- [ ] Test: nodo mesh hace HTTP request a Internet; dispositivo LAN hace ping a nodo mesh.

### Fase 10: Web UI + Prometheus (1 semana)

- [ ] Servidor HTTP con Digest Auth.
- [ ] Dashboard HTML mínimo + endpoints JSON.
- [ ] Endpoint `/metrics` Prometheus.

### Fase 11: Nodos Batería (1 semana)

- [ ] Ciclo SLEEP → TX UPLINK → RX1 → RX2 → SLEEP.
- [ ] Buffer de downlink en Parent.
- [ ] Lista de 3 Parents en NVS para redescubrimiento.
- [ ] Test: nodo batería cicla cada 60s, recibe mensajes downlink.

### Fase 12: ESP8266 (1 semana)

- [ ] `MeshNode8266` con protocolo Proxy.
- [ ] PROXY_DISCOVERY / PROXY_OFFER.
- [ ] Distribución de broker vía provisioning.
- [ ] Test: ESP8266 publica y recibe MQTT vía ESP32 proxy.

---

## 16. Integración con ESPHome (Reservado — v1.x)

> Esta sección no se implementa en v1.0. Define las decisiones de diseño que se deben respetar ahora para facilitar la integración futura.

### 16.1 Modelo de Integración

ESPHome es un sistema de configuración declarativa (YAML) para nodos ESP32/ESP8266 que genera firmware con componentes interconectados. La integración de EnigmaNG como **componente de transporte** en ESPHome seguiría el mismo patrón que su componente `wifi` o `ethernet`: se declara como `network:` y el resto de componentes (`mqtt:`, `sensor:`, `api:`) lo usan de forma transparente.

Hay dos niveles posibles de integración:

| Nivel | Descripción | Protocolo L3 |
|-------|-------------|-------------|
| **Nivel 1: IP transparente** | EnigmaNG expone un `WiFiClient`/socket lwIP. ESPHome usa su stack TCP/IP habitual (API nativa, MQTT, HTTP) sobre la mesh. Cero cambios en los componentes ESPHome existentes. | `PROTO_IPV4 (0x01)` |
| **Nivel 2: Protocolo nativo** | Componente ESPHome dedicado que habla el protocolo binario nativo de ESPHome (protobuf) directamente sobre frames L2 sin stack IP. Más eficiente para mensajes cortos de sensores. | `PROTO_ESPHOME (0x10)` |

**Recomendación para v1.x:** Implementar Nivel 1 primero. Es compatible con todos los componentes existentes y no requiere modificar ESPHome upstream. El Nivel 2 se puede añadir como optimización posterior.

### 16.2 Decisiones de Diseño Actuales que Facilitan la Integración

Las siguientes decisiones ya tomadas en v1.0 son favorables para ESPHome:

1. **Campo `Protocol` en el header (§4.1.6):** El valor `0x10` (`PROTO_ESPHOME`) está reservado. Un nodo con el módulo ESPHome habilitado simplemente registra un handler para ese Protocol ID. No es necesario cambiar el formato de frame.

2. **`getClient()` devuelve un `WiFiClient`:** La API pública ya expone un socket lwIP compatible. ESPHome puede sustituir su `WiFiClient` habitual por el de la mesh sin modificar la lógica de sus componentes.

3. **MTU 216 bytes y MSS 176 bytes:** El protocolo binario nativo de ESPHome (protobuf sobre TCP) genera mensajes típicos de 20–100 bytes para sensores de temperatura, interruptores, etc. Caben en un solo frame sin fragmentación.

4. **DHCP y tabla estática:** ESPHome gestiona IPs de nodos de forma declarativa en su configuración YAML. La asignación por tabla estática distribuida (§8.3, Modo A) es el mecanismo natural: la IP del nodo se fija en el YAML y se anuncia al join.

5. **Modo batería (§7):** ESPHome tiene soporte nativo para deep sleep (`deep_sleep:` component). El ciclo WAKE→TX→RX1→RX2→SLEEP de EnigmaNG es compatible con el modelo de ESPHome de "wake, measure, send, sleep".

### 16.3 Consideraciones para el Componente ESPHome

Cuando se implemente el componente ESPHome para EnigmaNG, los puntos clave serán:

- **Nombre del componente YAML:** `enigmang:` (como `wifi:` o `ethernet:`).
- **Configuración mínima:**

  ```yaml
  enigmang:
    psk: "mi_clave_secreta"
    mode: node        # node | gateway | battery
  ```

- **El componente debe registrarse como proveedor de red** (`network::`) en ESPHome, de forma que `mqtt:`, `api:`, `ota:`, etc. utilicen automáticamente la interfaz mesh en ausencia de WiFi.
- **OTA sobre mesh:** La actualización OTA de ESPHome (puerto 3232) funcionará sobre la interfaz IP de la mesh (Nivel 1). No requiere cambios adicionales.
- **Descubrimiento:** El componente puede anunciar el servicio `_esphomelib._tcp` vía el protocolo de descubrimiento propio de la mesh (§12), lo que permite que el dashboard de ESPHome descubra nodos mesh automáticamente.

---

## Apéndice A: Presupuesto de RAM (ESP32, estimación)

| Componente | RAM estimada |
| ------------ | ------------- |
| PeerManager (16 peers × 36B) | ~600 B |
| RouteTable (64 entries × 25B) | ~1.600 B |
| Seen-Frame Cache (32 entries × 12B) | ~384 B |
| Downlink buffer (5 nodos × 5 msg × 200B) | ~5.000 B |
| lwIP heap (TCP/IP stack) | ~20.000 B |
| Crypto (durante handshake, temporal) | ~2.000 B |
| Protocol dispatcher (tabla de handlers, ~8 entradas × 8B) | ~64 B |
| **Total estimado** | **~30 KB** |

ESP32 típico tiene 320KB de DRAM; hay margen suficiente para la aplicación del usuario.

---

## Apéndice B: Resumen de Decisiones de Diseño

| Decisión | Alternativa descartada | Razón |
| ---------- | ---------------------- | ------- |
| AES-128-GCM | AES-256-GCM | 128 bits suficientes para IoT; clave 2× más pequeña |
| Tag GCM 12 bytes | 16 bytes | NIST permite 12B; ahorra 4B/frame |
| Nonce derivado del header | Nonce transmitido | Ahorra 12B/frame; unicidad garantizada por (Epoch, Seq, SrcMAC) |
| Seen-Frame Cache | Campo Path en header | Ahorra hasta 49B/frame; estándar en 802.11s y BATMAN-adv |
| Epoch + Rechazo + Renegociación | Epoch + Overlap | Sin doble clave en RAM; latencia de rotación irrelevante (1×/día) |
| DVR proactivo (RIPv2-like) | OLSR, AODV, BATMAN | Overhead mínimo; escalable hasta ~100 nodos ESP-NOW |
| AP permanente en gateway | AP temporal periódico | ESP32 soporta AP+STA; onboarding inmediato |
| Routing LAN + NAT Internet | NAT puro | Permite acceso bidireccional LAN↔mesh |
| Broker en gateway, distribuido | Broker hardcoded en nodo | Configuración centralizada; sin reflasheo al cambiar broker |
| HTTP Digest Auth | Basic Auth / JWT | Sin password en claro; sin complejidad de TLS/JWT |
| Protocolo propio de servicios | mDNS estándar sobre mesh | mDNS es verbose para MTU 216B; mDNS estándar en interfaz WiFi del gateway |
| 3 Parents en NVS | Re-join completo siempre | Recuperación sin re-join en movilidad típica; coste: 20 bytes NVS |
| Certificados pospuestos a v1.1 | Modo CERT en v1.0 | PSK+ECDH suficiente; PKI añade complejidad de gestión innecesaria |
| Campo `Protocol` de 1 byte en header | Sin campo / 2 bytes | Multiplexación EtherType-like; 256 protocolos suficientes para red embebida; coste: 1B/frame |
| ESPHome: Nivel 1 (IP transparente) primero | Protocolo nativo directo | Compatible con todos los componentes existentes; Nivel 2 como optimización posterior |
| ICMP transparente (sin cambios) | Handler ICMP propio en mesh | lwIP gestiona ICMP en cualquier netif; cero overhead de diseño |
| Gateway dual-board (ESP-Hosted) | IDF nativo obligatorio | ESP-Hosted-MCU es componente IDF puro; Arduino Core no expone el sistema de componentes IDF |
