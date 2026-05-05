# Especificaciones Tecnicas: ESP32 Mesh Networking Library

**Version:** 1.0 — Especificacion para implementacion  
**Target:** Arduino Core ESP32 v3.3.8 (IDF 5.5.4) / ESP8266 (libreria derivada)  
**Base fisica:** QuickESPNow + ESP-NOW  
**Referencia arquitectonica:** EnigmaIOT

---

## 1. Stack Tecnologico y Versiones

| Componente | Version / Notas |
| --- | --- |
| **Arduino Core ESP32** | 3.3.8 (basado en IDF 5.5.4) |
| **Arduino Core ESP8266** | Ultima estable (libreria separada) |
| **Capa fisica** | QuickESPNow (wrapper sobre ESP-NOW) |
| **Crypto** | mbedTLS (incluido en IDF 5.5.4). Curva: Curve25519 para DH. Cifrado: AES-256-GCM (aceleracion hardware ESP32) o ChaCha20-Poly1305 (fallback software). |
| **Stack IP** | lwIP via esp_netif (API nativa IDF). Interfaz virtual personalizada. |
| **Web UI (gateway)** | esp_http_server (IDF nativo) o libreria AsyncWebServer compatible con Core 3.3.8. |
| **Metricas** | Endpoint HTTP /metrics en texto plano Prometheus. |
| **Almacenamiento claves** | NVS (ESP32) / EEPROM / SPIFFS (ESP8266). |

> **Nota IDF 5.5.4:** Se usaran APIs nativas de IDF (esp_netif, esp_wifi, esp_now, mbedtls) a traves de los wrappers de Arduino cuando sea posible, o directamente cuando Arduino no exponga la funcionalidad (ej. esp_netif_new() para el interfaz virtual).

---

## 2. Arquitectura de Capas

```text
┌─────────────────────────────────────────────────────────────┐
│  APLICACION                                                 │
│  - Clientes MQTT (PubSubClient, esp_mqtt_client)            │
│  - HTTP, mDNS-like, NTP-like                                │
│  - Web UI + Prometheus (solo gateways)                      │
├─────────────────────────────────────────────────────────────┤
│  TRANSPORTE (lwIP)                                          │
│  - TCP / UDP estandar                                       │
├─────────────────────────────────────────────────────────────┤
│  RED IP (Layer 3) — Interfaz Virtual "mesh0"                │
│  - IPv4 (v1.0) / IPv6 (v1.x)                                │
│  - netif custom (esp_netif)                                 │
│  - Fragmentacion L2 (MTU efectivo 200 bytes)                │
│  - DHCP server/client / Tabla estatica distribuida          │
├─────────────────────────────────────────────────────────────┤
│  ROUTING + ARP (Layer 2.5)                                  │
│  - Protocolo Mesh-DVR (Distance Vector hibrido)             │
│  - Tabla unificada IP↔MAC↔NextHop                           │
│  - Loop prevention (split horizon + path tracking)          │
│  - Control de memoria: pools con LRU + TTL                  │
├─────────────────────────────────────────────────────────────┤
│  ENLACE (Layer 2)                                           │
│  - Frame format con headers + MIC                           │
│  - Cifrado: AES-GCM por enlace (unicast) / Clave de Red     │
│    (broadcast)                                              │
│  - Handshake ECDH Curve25519 + autenticacion PSK            │
│  - Key rotation por epoch (overlap transparente)            │
│  - Peer Manager dinamico (sin limite hard, controlado por   │
│    memoria disponible)                                      │
├─────────────────────────────────────────────────────────────┤
│  FISICA (Layer 1)                                           │
│  - QuickESPNow (abstraccion ESP-NOW)                        │
│  - RSSI threshold + hysteresis                              │
│  - Canal unico (sincronizado con WiFi del bridge)           │
│  - Broadcast / Unicast / Fragmentacion L2                   │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. Capa Fisica (QuickESPNow / ESP-NOW)

### 3.1 Abstraccion minima requerida

```cpp
class MeshPhysicalLayer {
public:
    bool begin(uint8_t channel, const uint8_t* networkId);
    bool sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len);
    bool sendBroadcast(const uint8_t* data, size_t len);
    void onReceive(MeshRecvCallback cb);
    int8_t getLastRssi();  // RSSI del ultimo frame recibido
    void setChannel(uint8_t channel);
    bool setTxPower(int8_t power); // Opcional
};
```

### 3.2 Gestion de Canal

- **Canal unico:** Toda la red mesh opera en un canal WiFi especifico (1-14).
- **Bridge WiFi:** Si un nodo actua como bridge (conectado a AP WiFi externo), la mesh debe operar en el mismo canal que ese AP.
- **Cambio de canal:** Si el bridge cambia de AP (roaming) o el AP cambia de canal, el bridge debe:
  1. Anunciar CHANNEL_CHANGE_ADV con timestamp de migracion (ej: +30s).
  2. Todos los nodos cambian de canal y purgan peers (las claves de enlace se mantienen, pero se requiere re-verificar alcance).
  3. Los nodos que pierdan contacto durante el cambio inician busqueda ciega (ver §5.2).

### 3.3 RSSI y Umbral de Alcance

- **Conexion:** RSSI >= RSSI_CONNECT_THRESHOLD (default: -75 dBm).
- **Desconexion:** RSSI < RSSI_DISCONNECT_THRESHOLD (default: -85 dBm).
- **Hysteresis:** 10 dB de diferencia entre ambos umbrales para evitar flapping.
- **Medicion:** QuickESPNow proporciona RSSI por frame. Se promedia con EWMA (exponentially weighted moving average) sobre las ultimas 3-5 muestras. Tomar solamente las que estén en los últimos 15 minutos (configurable).

---

## 4. Capa de Enlace (Layer 2)

### 4.1 Formato de Frame

Todos los frames sobre ESP-NOW comparten este formato:

```text
[Magic: 2 bytes]          // 0x4D45 ("ME")
[Version: 1 byte]         // 0x01
[Network ID: 4 bytes]     // Derivado de la PSK
[Frame Type: 1 byte]      // Ver enum abajo
[Epoch: 1 byte]           // Epoch de clave actual (0-255, wrap-around)
[Src MAC: 6 bytes]
[Dst MAC: 6 bytes]        // FF:FF:FF:FF:FF:FF = broadcast
[Sequence: 2 bytes]       // Por enlace, anti-replay
[Path Len: 1 byte]        // 0-8 (para loop prevention en relay)
[Path: 6*N bytes]         // MACs visitadas (solo si Path Len > 0)
[Payload: variable]       // Cifrado (excepto JOIN_BEACON)
[MIC/Tag: 16 bytes]       // AES-GCM auth tag o Poly1305
```

**Frame Types:**

| Valor | Nombre | Descripcion |
| --- | --- | --- |
| 0x01 | JOIN_BEACON | Beacon de red (no cifrado o cifrado con clave de red) |
| 0x02 | KEY_EXCH_HELLO | Inicio handshake DH |
| 0x03 | KEY_EXCH_REPLY | Respuesta handshake DH |
| 0x04 | KEY_EXCH_CONFIRM | Confirmacion challenge |
| 0x05 | DATA | Payload IP o de aplicacion |
| 0x06 | DATA_FRAG | Fragmento de payload |
| 0x07 | ROUTE_ADV | Anuncio de rutas |
| 0x08 | ROUTE_WITHDRAW | Retiro de rutas |
| 0x09 | ARP_QUERY | Consulta IP→MAC (broadcast) |
| 0x0A | ARP_REPLY | Respuesta ARP |
| 0x0B | DHCP_REQUEST | Solicitud IP |
| 0x0C | DHCP_REPLY | Asignacion IP |
| 0x0D | CONTROL | Mensajes de control (sync, channel change, etc.) |
| 0x0E | PROXY_MSG | Mensaje ESP8266→Proxy (ver §10) |

### 4.2 Seguridad: Dos Anillos de Cifrado

#### A. Clave de Red (Network Key)

- Derivada de la PSK: NetworkKey = HKDF-SHA256(PSK, salt="network", info="broadcast").
- Usada para:
  - Cifrar JOIN_BEACON (para que nodos candidatos puedan verificar la red sin tener clave de enlace).
  - Cifrar mensajes de broadcast (ROUTE_ADV, ARP_QUERY, DHCP_REQUEST, etc.).
- **Ventaja:** Todos los nodos miembros pueden descifrar broadcast. Nodos de otra red (diferente PSK) descartan silenciosamente.

#### B. Clave de Enlace (Link Key)

- Negociada mediante ECDH Curve25519 entre cada par de nodos.
- **Handshake:**
  1. HELLO: Nodo A genera par efimero Curve25519, envia clave publica + nonce(32 bytes).
  2. REPLY: Nodo B genera su par, envia clave publica + nonce.
  3. Derivacion: SharedSecret = ECDH(privA, pubB). LinkKey = HKDF(SharedSecret, salt=PSK, info="link-" + macA + macB).
  4. CONFIRM: Ambos envian un challenge cifrado con LinkKey para validar.
- **Autenticacion PSK:** El salt del HKDF incluye la PSK. Si la PSK es incorrecta, el LinkKey derivado no coincidira y el challenge falla.
- **Anonimato forward:** Las claves efimeras se regeneran en cada rotacion.

### 4.3 Key Rotation (Epoch)

- Cada KEY_ROTATION_INTERVAL (configurable, default 86400s), el epoch se incrementa.
- **Transparencia:** Cuando un nodo envia un frame con epoch N+1, el receptor detecta el salto e inicia re-negociacion DH automaticamente.
- **Overlap:** Durante 2 * KEY_ROTATION_INTERVAL, se aceptan frames con epoch N y N+1.
- **Nodos bateria:** Al despertar, si su epoch esta desfasado, envian un KEY_EXCH_HELLO con un flag de "recovery" durante su ventana RX.
  
  **Valorar si en vez de gestionar dos claves la rotación de clave sería de esta otroa forma:**
- No hace falta epoch
- Transparencia en capa 3: Cuando un nodo envía un mensaje cifrado con una clave antigua el receptor rechaza el mensaje informando del error "clave incorrecta". El emisor hace una renegociación de clave y si es satisfactoria se reenvía el mensaje que fue rechazado
- No hay overlap
- Los nodos batería se gestionan del mismo modo.
  
  ¿CUÁL ES MÁS EFICIENTE?

### 4.4 Peer Manager

```cpp
struct PeerEntry {
    uint8_t mac[6];
    uint8_t linkKey[32];
    uint8_t epoch;
    int8_t  avgRssi;
    bool    canRelay;      // Capacidad anunciada en handshake
    uint32_t lastSeen;
    uint32_t lastSeq;      // Anti-replay
    uint16_t routeCount;   // Cuantas rutas pasan por este peer
};
```

- **Almacenamiento:** Mapa en RAM (std::map o hash table abierto). No hay limite hard de peers gracias a QuickESPNow, pero si control de memoria.
- **Eviccion LRU:** Si freeHeap < PEER_MANAGER_HEAP_THRESHOLD (ej: 20KB libres), se evicta el peer con lastSeen mas antiguo y routeCount == 0.
- **Cleanup periodico:** Cada 60s, eliminar peers con lastSeen > PEER_TIMEOUT (default 3600s).

---

## 5. Onboarding y Descubrimiento de Canal

Este es uno de los puntos mas criticos porque la capa 2 esta cifrada y un nodo nuevo no tiene clave de enlace.

### 5.1 Mecanismo Principal: AP Temporal (tipo EnigmaIOT)

- **Trigger:** El bridge (o un nodo designado "announcer") activa un AP WiFi temporal periodicamente.
- SSID: `MESH-<NetworkID_HEX>-<CHANNEL>`
- Password: la PSK de la red (texto plano).
- Duracion: AP_BEACON_DURATION (default 60s) cada AP_BEACON_INTERVAL (default 10 min). TENDRÍA SENTIDO AP PERMANENTE EN EL GATEWAY
- **Protocolo:** El nodo nuevo se conecta a este AP via WiFi estandar. A traves de un mini-servidor HTTP (o socket TCP simple) en el puerto 80, el AP responde con:
  
```json
  {
    "network_id": "A1B2C3D4",
    "channel": 6,
    "psk_hash": "...",        // Para verificacion
    "gateway_macs": ["AA:BB:CC:DD:EE:FF"], // Seeds
    "ip_range": "10.200.0.0/16"
  }
```

- El nodo nuevo desconecta del AP, configura ESP-NOW en el canal indicado, y envia JOIN_BEACON cifrado con la Network Key (que puede derivar de la PSK).

### 5.2 Fallback: Busqueda Ciega de Canal

- Si no se detecta AP tras AP_SCAN_TIMEOUT (ej: 2 minutos), el nodo entra en modo busqueda ciega:
  1. Itera canales 1, 6, 11 (y luego el resto si es necesario).
  2. En cada canal, escucha ESP-NOW durante CHANNEL_DWELL_TIME (200ms).
  3. Busca frames JOIN_BEACON con Magic correcto y Network ID derivado de su PSK.
  4. Si encuentra uno valido, se queda en ese canal e inicia handshake.
- **Optimizacion:** Los JOIN_BEACON deben enviarse con cierta frecuencia (ej: cada 5s) por los nodos ya unidos (o solo por el bridge), para que la busqueda ciega sea rapida.

### 5.3 Propagacion del Canal a Nodos No en Alcance del Bridge

- Los nodos relay incluyen el canal actual en sus ROUTE_ADV.
- Un nodo que se une a traves de un relay (no directamente al bridge) recibe el canal via ROUTE_ADV de su vecino.
- Si un nodo detecta inconsistencia de canal (vecinos en canales diferentes), prioriza el canal del nodo con menor hop-count al bridge.

> 🔴 CUESTION ABIERTA: ¿La busqueda ciega debe ser el mecanismo primario y el AP temporal secundario? El AP consume mas energia y requiere que el bridge alterne WiFi STA ↔ AP. Validar en pruebas de campo cual es mas fiable.
>
> Los AP serán normalmente los gateways que nunca se alimentan con baterías. ESP32 puede hacer AP y STA simultáneo. Aunque es la misma radio tiene dos MAC.

---

## 6. Protocolo de Routing (Mesh-DVR)

### 6.1 Diseno: Distance Vector Routing Hibrido

Dado que la red puede tener dimension indeterminada, se opta por un protocolo proactivo ligero (no reactivo como AODV, para evitar latencia de descubrimiento en cada comunicacion).

**Principios:**

- Cada nodo conoce la ruta completa (next hop) hacia cualquier destino, pero no el path completo.
- No se usa link-state (OSPF-like) porque el overhead de broadcast de topologia completa no escala en ESP-NOW.

### 6.2 Route Advertisement (RA)

- **Frecuencia:** Cada RA_INTERVAL (default 30s).
- **Destino:** Broadcast ESP-NOW (cifrado con Network Key).
- **Contenido:** Lista de entradas (IP, MAC, HopCount, Flags) que el nodo alcanza.
  - Incluye si mismo (hopCount = 0).
  - Incluye las rutas aprendidas de otros, con hopCount + 1.
- **Procesamiento:** Al recibir un RA, un nodo actualiza su tabla si:
  - La ruta es nueva, O
  - El hopCount ofrecido es menor que el actual, O
  - El hopCount es igual pero el RSSI al nextHop es mejor.

### 6.3 Tabla de Routing / ARP Unificada

```cpp
struct RouteEntry {
    uint8_t  ip[16];          // IPv6-ready (16 bytes)
    uint8_t  mac[6];          // MAC del destino final
    uint8_t  nextHop[6];      // MAC del siguiente salto
    uint8_t  hopCount;        // 0 = local, 255 = infinito
    int8_t   rssi;            // RSSI al nextHop
    uint32_t lastUpdate;      // millis()
    uint16_t ttl;             // Segundos hasta expirar (default 300)
    bool     isDirect;        // Vecino directo (hopCount == 0 o 1)
    bool     viaRelay;        // Si el nextHop tiene canRelay=true
};
```

**Control de memoria:**

- **Pool fijo:** Array estatico de MAX_ROUTES entradas (configurable en tiempo de compilacion, default 64).
- **Politica de eviccion:** Cuando la tabla esta llena y llega una nueva ruta:
  1. Eliminar entradas expiradas (ttl == 0).
  2. Si sigue llena, eliminar la ruta con mayor hopCount (menos prioritaria).
  3. Si hay empate, eliminar la menos recientemente actualizada.
- **ARP implicito:** No existe tabla ARP separada. La resolucion IP→MAC es una consulta a esta tabla. Si no existe, se envia ARP_QUERY broadcast.

### 6.4 Prevencion de Bucles

1. **Split Horizon:** Un nodo no anuncia en un RA enviado al peer X las rutas cuyo nextHop es X.
2. **Poison Reverse:** Si una ruta se aprendio de X, se anuncia a X con hopCount = 255 (infinite).
3. **Path Tracking (DATA frames):** Cada frame DATA lleva en el header la lista de MACs visitadas (Path). Maximo 8 hops. Si un nodo se encuentra en la lista, descarta el frame.
4. **TTL IP:** El stack lwIP ya decrementa TTL. Cuando TTL llega a 0, se descarta.

### 6.5 Route Withdraw

- Si un peer desaparece (no se recibe nada en PEER_TIMEOUT), el nodo envia ROUTE_WITHDRAW broadcast con la MAC del peer caido.
- Todos los nodos eliminan rutas donde nextHop == mac_caido o mac_destino == mac_caido.
- **Optimizacion:** No enviar withdraw si el peer no era nextHop de ninguna ruta activa.

### 6.6 Reorganizacion Automatica

- La topologia se reorganiza naturalmente por el intercambio periodico de RA.
- Si aparece una ruta mejor (menor hopCount o mejor RSSI), se adopta inmediatamente.
- Si desaparece un enlace, las rutas expiran por TTL o por withdraw, y se reconvergen.

> 🔴 CUESTION ABIERTA: ¿Es suficiente el TTL de 300s para convergencia rapida, o es demasiado lento? En una red movil podria necesitarse un RA_INTERVAL mas agresivo (5-10s) a costa de mas trafico. Definir en pruebas.

---

## 7. Nodos Bateria y Ventanas de Recepcion (LoRaWAN-style)

### 7.1 Ciclo de Vida

```text
[SLEEP PROFUNDO] --(T1)--> [WAKE] --> [ENVIAR UPLINK] --> [RX1: 2s] --> [RX2: 2s] --> [SLEEP]
```

- **UPLINK:** El nodo envia sus datos (sensores, heartbeat) a su Parent Node (nodo relay elegido).
- **RX1:** Escucha ESP-NOW durante 2 segundos. El Parent envia cualquier mensaje pendiente.
- **RX2:** Segunda ventana de 2 segundos (por si el Parent estaba ocupado retransmitiendo).
- **Vuelta a sleep.**

### 7.2 Parent Node

- El nodo bateria elige como Parent al vecino relay con mejor RSSI durante el join.
- El Parent mantiene un buffer de downlink por hijo (cola FIFO, maximo 5 mensajes, maximo 200 bytes cada uno).
- Cuando el Parent recibe un frame destinado a un hijo bateria, lo almacena en el buffer.
- Al recibir el UPLINK, el Parent vacia el buffer en RX1/RX2.

### 7.3 Heartbeat

- Los nodos bateria envian un HEARTBEAT periodico cada BATTERY_HEARTBEAT_INTERVAL (default: 1 hora).
- Esto permite al Parent saber que siguen vivos y entregar mensajes acumulados.
- Si un Parent no recibe heartbeat en 3 * BATTERY_HEARTBEAT_INTERVAL, libera el buffer y retira las rutas del hijo.

### 7.4 Sincronizacion de Reloj (NTP-like)

- El Parent incluye su timestamp (segundos desde epoch) en la respuesta a UPLINK.
- El nodo bateria calcula el offset y lo guarda en RTC memory.
- Usado para:
  - Programar wake-ups precisos (aunque el reloj RTC del ESP32 tiene deriva, es suficiente para ventanas de segundos).
  - Key rotation (sabe cuando su clave expira).
  - Timestamps en metricas.

> 🔴 CUESTION ABIERTA: ¿Que pasa si el nodo bateria se mueve y pierde contacto con su Parent? Necesita un mecanismo de Parent Rediscovery al despertar si no recibe ACK del Parent. Esto implica escanear vecinos y posiblemente re-join, lo que consume energia. Definir estrategia de re-join energeticamente eficiente.

---

## 8. Capa IP (IPv4/IPv6) y MTU

### 8.1 Interfaz Virtual (netif)

- Crear un esp_netif custom en modo STA-like (o mejor, un nuevo tipo MESH).
- **Input path:** Frames DATA descifrados se inyectan en netif->input().
- **Output path:** lwIP llama a mesh_netif_output(). El paquete IP se encapsula en frames Layer 2.

### 8.2 MTU y Fragmentacion

- **MTU de la interfaz mesh:** 200 bytes.
- **Overhead L2:** ~35 bytes (headers + MIC).
- **Payload ESP-NOW efectivo:** 200 bytes de paquete IP.
- **Fragmentacion L2:** Si un paquete IP supera 200 bytes, la capa L2 lo fragmenta:

  ```text
  [Frag ID: 2 bytes] [Frag Num: 1 byte] [Total Frags: 1 byte] [Payload: <=200 bytes]
  ```

- **Reensamblaje:** Buffer por Frag ID en el receptor. Timeout: 2 segundos. Si falta un fragmento, descartar todo.
- **TCP:** Dado el MTU bajo, el rendimiento TCP sera pobre (muchos ACKs, fragmentacion). Se recomienda UDP para aplicaciones de la mesh. TCP funciona pero no es optimo.
  
  ESTUDIAR TAMAÑO DE VENTANA Y VALORAR VENTAJAS O INCONVENIENTES DE USAR FRAGMENTACIÓN CON IP

### 8.3 Asignacion de Direcciones IP

#### Modo A: Tabla Estatica Distribuida (Recomendado para nodos bateria)

- El gateway/bridge mantiene una tabla MAC → IPv4 en NVS.
- Esta tabla se distribuye periodicamente dentro de los ROUTE_ADV (campo opcional).
- Los nodos almacenan su IP en NVS. Al unirse, la anuncian.
- **Ventaja:** Un nodo bateria que despierta ya sabe su IP. No necesita DHCP handshake.

#### Modo B: DHCP

- Servidor DHCP corriendo en el bridge (usando servidor lwIP o implementacion custom).
- Los nodos cliente envian DHCP_REQUEST broadcast mesh.
- **Desventaja:** Requiere intercambio de 4 mensajes. Los nodos bateria deben permanecer despiertos durante todo el handshake.

#### Modo C: IP Fija

- Configurable manualmente por el usuario en MeshNetwork.begin().

**Subred:** Todas las IPs en la misma subred (ej: 10.200.0.0/16). El bridge hace NAT hacia la red WiFi externa.

### 8.4 ARP

- **Resolucion local:** Consulta a la tabla de routing unificada. Si existe, resuelve inmediatamente.
- **Resolucion remota:** Si no existe la IP en la tabla, se envia ARP_QUERY broadcast mesh. El nodo propietario responde con ARP_REPLY unicast.
- **Cache:** Las respuestas ARP se insertan en la tabla de routing con TTL corto (ej: 60s).

> 🔴 CUESTION ABIERTA: IPv6 no se implementara en la v1.0. Se deja preparado el formato de 16 bytes en las estructuras, pero el stack IPv6 requiere ND (Neighbor Discovery) que complica el diseno. Evaluar para v1.1.

---

## 9. Bridge WiFi / Gateway

### 9.1 Funcion

- Conexion simultanea: WiFi STA (a AP externo) + ESP-NOW mesh.
- Actua como NAT gateway para la subred mesh.
- Puede haber multiples bridges por redundancia.

### 9.2 NAT

- **Masquerading:** Todos los paquetes salientes de la mesh se enmascaran con la IP WiFi del bridge.
- **Port mapping:** No se implementa UPnP. Las conexiones entrantes requieren configuracion manual o no son soportadas (la mesh es outbound-only hacia Internet, salvo conexiones establecidas desde dentro).
  
  NO ES DEL TODO CORRECTO. VALORAR LA IMPLEMENTACIÓN DE ROUTING PARA PERMITIR MENSAJES DESDE LA RED WIFI A LA MESH. VALORAR AMBAS SOLUCIONES INCLUSO UNA SOLUCIÓN MIXTA.

### 9.3 Seleccion de Gateway

- Los bridges anuncian su presencia en ROUTE_ADV con un flag IS_GATEWAY y su metrica (ej: calidad de conexion WiFi).
- Los nodos eligen como default gateway el bridge con:
  1. Menor hopCount.
  2. Si empate, mejor RSSI al nextHop.
  3. Si empate, menor carga (numero de nodos reportados).

### 9.4 Redundancia

- Si un bridge desaparece, las rutas hacia el expiran y los nodos migran al siguiente bridge disponible automaticamente.
- **Problema:** Las conexiones TCP activas se rompen al cambiar de gateway (diferente IP publica). Esto es aceptable para una v1.0.

---

## 10. Soporte ESP8266 (Libreria Derivada)

Se acepta que sea una libreria separada para facilitar mantenibilidad.

### 10.1 Limitaciones

- No relay.
- No stack IP (lwIP en ESP8266 no permite netif virtual de forma sencilla en Arduino).
- No bridge.

### 10.2 Arquitectura: Proxy MQTT-like

- El ESP8266 se comunica con un nodo ESP32 vecino (Parent/Proxy) mediante ESP-NOW.
- Protocolo de mensajeria propio (no IP):

```cpp
enum ProxyMsgType : uint8_t {
    PROXY_CONNECT = 0x01,    // Registro en el proxy
    PROXY_PUBLISH = 0x02,    // Publicar en topic MQTT
    PROXY_SUBSCRIBE = 0x03,  // Suscribirse a topic
    PROXY_UNSUBSCRIBE = 0x04,
    PROXY_MESSAGE = 0x05,    // Mensaje recibido del broker
    PROXY_ACK = 0x06,        // ACK generico
    PROXY_DISCONNECT = 0x07
};
```

- **Fragmentacion:** Permitida en este protocolo. Los mensajes PROXY_* pueden superar 250 bytes y se fragmentan en la capa L2.
- **Proxy ESP32:** Recibe los mensajes PROXY_*, traduce a MQTT usando PubSubClient conectado al broker via WiFi/Ethernet, y reenvia las respuestas al ESP8266.
- **Descubrimiento:** El ESP8266 envia PROXY_DISCOVERY broadcast. Los ESP32 vecinos responden PROXY_OFFER. El ESP8266 elige el de mejor RSSI.
  
  AUNQUE NO HAYA DIRECCIÓN IP ES NECESARIO SABER LA DIRECCIÓN DEL BROKER. VALORAR SI CONFIGURARLO EN EL/LOS GATEWAYS O EN EL NODO ESP8266. (IGUAL ESTA SEGUNDA OPCIÓN ES MÁS TRANSPARENTE)
  
  PENSAR SI ES NECESARIA CONFIRMACIÓN DE ENTREGA. ESPNOW CONFIRMA LOS MENSAJES PEER TO PEER.

### 10.3 API para ESP8266

```cpp
class MeshNode8266 {
public:
    bool begin(const char* psk);
    bool mqttPublish(const char* topic, const uint8_t* payload, size_t len, bool retain = false);
    bool mqttSubscribe(const char* topic);
    void onMqttMessage(MqttCallback cb);
    void setSleepDuration(uint32_t seconds); // Sleep entre heartbeats
};
```

> 🔴 CUESTION ABIERTA: ¿Vale la pena intentar una integracion "transparente" para ESP8266 (ej. implementar un netif en lwIP del ESP8266) o es mejor aceptar el proxy y no perder tiempo? Mi recomendacion es el proxy. Confirmar decision.

---

## 11. Web UI y Metricas Prometheus (Gateway)

### 11.1 Servidor Web

- esp_http_server (IDF nativo) o libreria AsyncWebServer compatible con Core 3.3.8.
- **Endpoints:**
  - GET / — Dashboard HTML con topologia, tabla de nodos, estado de enlaces.
  - GET /api/v1/status — JSON con estado general.
  - GET /api/v1/nodes — Lista de nodos conectados.
  - GET /api/v1/routes — Tabla de rutas.
  - GET /metrics — Prometheus text format.
  - POST /api/v1/config — Cambio de configuracion (requiere auth).

### 11.2 Metricas Prometheus (Endpoint /metrics)

```text
# HELP mesh_nodes_total Nodos activos en la red mesh
# TYPE mesh_nodes_total gauge
mesh_nodes_total{network="red1"} 15

# HELP mesh_routes_total Entradas en tabla de routing
# TYPE mesh_routes_total gauge
mesh_routes_total{network="red1"} 32

# HELP mesh_link_rssi RSSI del enlace directo
# TYPE mesh_link_rssi gauge
mesh_link_rssi{src="AABBCCDDEEFF",dst="112233445566"} -72

# HELP mesh_key_epoch Epoch de clave por enlace
# TYPE mesh_key_epoch gauge
mesh_key_epoch{link="AABBCCDDEEFF_112233445566"} 42

# HELP mesh_uptime_seconds Tiempo activo del nodo
# TYPE mesh_uptime_seconds counter
mesh_uptime_seconds{node="AABBCCDDEEFF"} 86400

# HELP mesh_battery_voltage Voltaje de bateria (si aplica)
# TYPE mesh_battery_voltage gauge
mesh_battery_voltage{node="AABBCCDDEEFF"} 3.65

# HELP mesh_packets_total Total de paquetes procesados
# TYPE mesh_packets_total counter
mesh_packets_total{node="AABBCCDDEEFF",direction="rx"} 15000
```

### 11.3 Autenticacion Web

- **Opcion 1:** Basic Auth con credenciales configurables.
- **Opcion 2:** Token JWT (mas seguro, mas complejo).
- **Recomendacion v1.0:** Basic Auth sobre HTTPS si es posible, o HTTP + token simple.

---

## 12. mDNS / Descubrimiento de Servicios

- Implementar un protocolo ligero propio sobre broadcast mesh (no mDNS estandar, que es muy verbose para 200 bytes MTU).
- Cada nodo puede registrar servicios: _http._tcp, _mqtt._tcp.
- Los registros se incluyen en ROUTE_ADV o en mensajes SERVICE_ADV periodicos.
- Resolucion: Un nodo consulta por nombre de servicio → broadcast mesh → el nodo propietario responde con IP + puerto.

> 🔴 CUESTION ABIERTA: ¿Implementar mDNS estandar (lwIP lo soporta) encapsulado en frames L2, o un protocolo propio mas ligero? mDNS estandar genera mucho trafico multicast. Recomiendo protocolo propio para v1.0.

---

## 13. Autenticacion Mutua con Certificados (Opcional)

### 13.1 Modo PSK (default)

- Como se describe en §4.2. Rapido, ligero, suficiente para la mayoria de casos.

### 13.2 Modo CERT (opcional, configuracion en compile-time o runtime)

- Cada nodo tiene un par de claves RSA-2048 o ECC P-256 y un certificado X.509.
- Durante el handshake DH, ambos nodos intercambian sus certificados y firman sus claves publicas efimeras.
- **Optimizacion de rendimiento:**
  - El certificado se almacena en NVS y se carga en RAM solo durante el handshake.
  - La verificacion de certificado usa la cadena de confianza pre-instalada (CA raiz en el firmware).
  - No se usa TLS sobre la mesh (demasiado overhead). Solo se usan certificados para autenticar el handshake DH.
- **Impacto:** Aumenta el tamano del firmware (~10-20KB por certificado) y el tiempo de handshake (~500ms vs ~100ms). No afecta el throughput de datos una vez establecido el enlace.

> 🔴 CUESTION ABIERTA: ¿El modo CERT es realmente necesario para v1.0, o se deja como extension futura? Recomiendo dejarlo para v1.1 y centrarse en PSK para validar la arquitectura primero.

---

## 14. API Publica de la Libreria (Boceto C++)

```cpp
// MeshNetwork.h
class MeshNetwork {
public:
    // Inicializacion
    bool begin(const char* psk, MeshMode mode = MESH_NODE);
    bool begin(const char* psk, IPAddress staticIP, MeshMode mode = MESH_NODE);
    
    // Configuracion
    void setRelayEnabled(bool enabled);           // Default: true
    void setBatteryMode(bool enabled, uint32_t sleepIntervalSec);
    void setRssiThreshold(int8_t connectDbm, int8_t disconnectDbm);
    void setKeyRotationInterval(uint32_t seconds);
    
    // Estado
    bool isConnected();
    bool isGateway();                             // true si tiene WiFi uplink
    int  getNodeCount();                          // Nodos conocidos en la red
    int8_t getRssiTo(const uint8_t* mac);
    int8_t getRssiFromGateway();
    
    // IP Interface (para integracion transparente)
    // Devuelve un Client& que puede usarse con PubSubClient, etc.
    Client& getClient();
    
    // Callbacks
    void onMessage(MeshMessageCallback cb);       // Datos recibidos (no IP)
    void onNodeJoin(MeshNodeCallback cb);
    void onNodeLeave(MeshNodeCallback cb);
    
    // Gateway-only
    bool startWebServer(uint16_t port = 80);
    bool startPrometheus(uint16_t port = 9090);
    bool setStaticIPTable(const std::map<String, IPAddress>& table);
    
    // NTP-like sync
    time_t getMeshTime();                         // Tiempo sincronizado
    void onTimeSync(MeshTimeCallback cb);
    
    // Control
    void loop();                                  // Debe llamarse en loop()
    void shutdown();                              // Desconectar gracefully
};

enum MeshMode {
    MESH_NODE,        // Nodo estandar
    MESH_GATEWAY,     // Con WiFi uplink
    MESH_BATTERY      // Nodo bateria (relay off, sleep ciclico)
};
```

---

## 15. Plan de Desarrollo Incremental

El objetivo es tener cada fase testeable de forma aislada antes de acumular complejidad.

### Fase 0: Infraestructura y Pruebas de Concepto (1 semana)

- [ ] Setup de proyecto PlatformIO / Arduino con Core 3.3.8 + QuickESPNow.
- [ ] Verificar compatibilidad de QuickESPNow con IDF 5.5.4.
- [ ] Test basico ESP-NOW: envio/recepcion entre 2 ESP32, medicion de RSSI, cambio de canal.
- [ ] Definir estructura de directorios del repo (src/, examples/, tests/).

### Fase 1: Capa Fisica + Frame Format (1 semana)

- [ ] Implementar MeshPhysicalLayer (wrapper QuickESPNow).
- [ ] Definir y serializar el formato de frame L2 (sin cifrado).
- [ ] Implementar PeerManager basico (anadir/eliminar peers, RSSI promedio).
- [ ] Test: 3 nodos enviando frames entre si, filtrado por Network ID.

### Fase 2: Seguridad — Handshake y Cifrado (2 semanas)

- [ ] Integrar mbedTLS: ECDH Curve25519, HKDF, AES-GCM.
- [ ] Implementar handshake completo (HELLO → REPLY → CONFIRM).
- [ ] Cifrado/descifrado de frames unicast con Link Key.
- [ ] Cifrado de broadcast con Network Key.
- [ ] Test: 2 nodos negocian clave, envian datos cifrados, verificar que nodos con PSK diferente no pueden unirse.

### Fase 3: Key Rotation y Peer Manager Avanzado (1 semana)

- [ ] Implementar epoch y rotacion transparente.
- [ ] Eviccion LRU y control de memoria en PeerManager.
- [ ] Test: Forzar rotacion, verificar que no hay perdida de paquetes.

### Fase 4: Routing Basico — Vecinos y Saltos (2 semanas)

- [ ] Implementar ROUTE_ADV y procesamiento de tabla.
- [ ] Routing unicast directo y un salto (relay).
- [ ] Loop prevention basico (split horizon).
- [ ] Test: 3 nodos en linea (A -- B -- C). A envia a C via B.

### Fase 5: Routing Multi-hop y Control de Memoria (1 semana)

- [ ] Tabla de routing unificada con limite de entradas y eviccion.
- [ ] ROUTE_WITHDRAW.
- [ ] Path tracking en frames DATA.
- [ ] Test: Red de 5 nodos, desconectar un nodo central, verificar reconvergencia.

### Fase 6: Interfaz IP Virtual (netif) — IPv4 (2 semanas)

- [ ] Crear esp_netif custom en IDF.
- [ ] Inyeccion de paquetes RX en lwIP.
- [ ] Captura de paquetes TX desde lwIP hacia la mesh.
- [ ] Test: Ping entre dos nodos mesh usando IPs estaticas.

### Fase 7: MTU, Fragmentacion L2 y ARP (1 semana)

- [ ] Implementar fragmentacion/reensamblaje en Layer 2.
- [ ] ARP integrado en tabla de routing.
- [ ] Test: Enviar paquete UDP de 500 bytes entre nodos. Verificar fragmentacion.

### Fase 8: DHCP y Tabla Estatica (1 semana)

- [ ] Implementar asignacion por tabla estatica distribuida.
- [ ] Servidor DHCP basico en gateway.
- [ ] Test: Nodo obtiene IP automaticamente. Nodo bateria recupera IP de NVS.

### Fase 9: Bridge WiFi + NAT (2 semanas)

- [ ] Modo gateway: WiFi STA + ESP-NOW simultaneo.
- [ ] NAT/masquerading con lwIP (ip_napt si esta disponible en IDF 5.5.4, o implementacion custom).
- [ ] Test: Nodo mesh hace HTTP request a Internet a traves del gateway.

### Fase 10: Integracion Client Transparente (1 semana)

- [ ] Exponer Client& compatible con WiFiClient.
- [ ] Test: PubSubClient publicando a broker MQTT publico a traves de la mesh.

### Fase 11: Onboarding y Descubrimiento de Canal (1 semana)

- [ ] AP temporal en gateway.
- [ ] Busqueda ciega de canal.
- [ ] Protocolo de join seguro.
- [ ] Test: Nodo nuevo se une a red existente sin configuracion previa de canal.

### Fase 12: Nodos Bateria y Ventanas RX (2 semanas)

- [ ] Modo bateria: sleep ciclico, uplink, RX1/RX2.
- [ ] Parent node y buffer de downlink.
- [ ] Heartbeat periodico.
- [ ] Test: Nodo bateria envia datos cada 5 min. Gateway le envia comando y lo recibe en la siguiente ventana.

### Fase 13: NTP-like Sync y Metricas (1 semana)

- [ ] Sincronizacion de tiempo padre→hijo.
- [ ] Endpoint /metrics Prometheus en gateway.
- [ ] Recoleccion de metricas de nodos y agregacion.

### Fase 14: Web UI (1 semana)

- [ ] Dashboard HTML basico.
- [ ] Visualizacion de topologia (tabla + grafo simple).
- [ ] Configuracion de red via web.

### Fase 15: ESP8266 — Libreria Derivada (2 semanas)

- [ ] Implementar MeshNode8266 con protocolo PROXY.
- [ ] Fragmentacion en capa PROXY.
- [ ] Test: ESP8266 publica MQTT via proxy ESP32.

### Fase 16: mDNS-like y Pulido Final (1 semana)

- [ ] Protocolo ligero de descubrimiento de servicios.
- [ ] Optimizaciones de memoria y estabilidad.
- [ ] Documentacion y ejemplos.

### Fase 17 (Futuro): Certificados y IPv6

- [ ] Modo CERT opcional.
- [ ] Soporte IPv6 en netif.

NO TE OLVIDES DE CREAR TESTS UNITARIOS PARA CADA FASE, BIEN DOCUMENTADOS Y JUSTIFICADOS. QUE SE PUEDAN EJECUTAR EN EL PC.

DISEÑA PRUEBAS CLARAS DE INTEGRACIÓN, PREMIANDO LA SIMPLICIDAD PERO CUBRIENDO SUFICIENTES CASOS DE USO

---

## 16. Resumen de Cuestiones Abiertas

| # | Tema | Impacto | Decision Pendiente |
|---|------|---------|-------------------|
| 1 | Busqueda ciega vs AP como primario | UX y energia del gateway | ¿Hacer la busqueda ciega el mecanismo principal para reducir consumo del bridge? |
| 2 | RA_INTERVAL optimo | Trafico de control vs convergencia | ¿30s es correcto? Validar en redes de >20 nodos. |
| 3 | Re-join de nodos bateria moviles | Fiabilidad | ¿Como detectar que el Parent desaparecio sin gastar bateria en escaneo constante? |
| 4 | IPv6 en v1.0 | Alcance del proyecto | ¿Posponer a v1.1? Recomendado: SI, posponer. |
| 5 | Modo CERT para v1.0 | Complejidad y tiempo | ¿Incluir o posponer a v1.1? Recomendado: Posponer. |
| 6 | ESP8266: Proxy vs IP real | Arquitectura derivada | ¿Confirmar que aceptamos proxy y libreria separada? |
| 7 | mDNS estandar vs protocolo propio | Trafico en la mesh | ¿Implementar protocolo propio ligero? Recomendado: SI. |
| 8 | NAT en IDF 5.5.4 | Disponibilidad de API | Verificar si ip_napt esta expuesto en lwIP de IDF 5.5.4 o requiere parche. |
| 9 | AsyncWebServer en Core 3.3.8 | Compatibilidad | Validar si ESPAsyncWebServer funciona sin modificaciones en Core 3.3.8. |

---

## 17. Recomendaciones Finales para el Agente de Programacion

1. **Empezar por Fase 0-2.** No avanzar a IP virtual hasta que el cifrado y routing basico sean solidos. Es mucho mas facil debuggear frames L2 sin la complejidad de lwIP en medio.
2. **Usar logging extensivo.** Implementar un sistema de log por niveles (DEBUG, INFO, WARN, ERROR) con salida por Serial. En la mesh, anadir un flag para enviar logs al gateway (util para debug remoto).
3. **Test unitarios en host.** Donde sea posible (crypto, serializacion de frames, logica de routing), escribir tests que corran en Linux/PC antes de flashear al ESP32.
4. **Documentar limites de memoria.** La RAM del ESP32 es el cuello de botella. Medir heap libre en cada fase.
5. **No optimizar prematuramente.** El MTU de 200 bytes es aceptable para la v1.0. Si TCP es lento, documentarlo y recomendar UDP.
