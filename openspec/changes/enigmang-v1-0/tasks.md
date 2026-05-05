# Tareas: EnigmaNG v1.0

## Progreso: 51/74 tareas completadas

---

## Fase 0: Infraestructura del proyecto

- [x] Configurar PlatformIO con Arduino Core ESP32 3.3.8 + QuickESPNow
  - _Test: `pio run` sin errores de compilación_
- [ ] Verificar compatibilidad de QuickESPNow con IDF 5.5.4
  - _Test: envío/recepción básico ESP-NOW en 2 ESP32 con QuickESPNow_
- [ ] Test ESP-NOW básico: envío unicast/broadcast, obtención de RSSI por frame
  - _Test: 3 nodos, todos reciben broadcast, RSSI devuelto por QuickESPNow_
- [x] Crear estructura de directorios: `src/`, `arduino/`, `idf_component/`, `examples/`, `test/`
  - _Test: `library.json` válido, PlatformIO lo reconoce como librería_
- [x] Configurar framework de tests (Unity en PlatformIO)
  - _Test: test dummy pasa en `pio test`_

---

## Fase 1: Capa Física

**Spec:** `openspec/specs/physical-layer/spec.md`

- [x] Implementar `MeshPhysicalLayer` como wrapper sobre QuickESPNow
  - _Test: `begin(channel=6, networkId)` inicializa ESP-NOW correctamente_
- [x] Implementar `sendUnicast()` y `sendBroadcast()`
  - _Test: frame unicast llega solo al destinatario; broadcast a todos en el canal_
- [x] Implementar RSSI EWMA (α=0.3) con `PEER_INACTIVITY_TIMEOUT`
  - _Test: 10 frames recibidos con RSSI conocido; verificar cálculo EWMA_
- [x] Implementar gestión de canal (`setChannel()`) y anuncio `CONTROL/CHANNEL_CHANGE`
  - _Test: 2 nodos cambian de canal sincronizados; retoman comunicación sin renegociar clave_

---

## Fase 2: Serialización de frame (Link Layer)

**Spec:** `openspec/specs/link-layer/spec.md`

- [x] Implementar serializador/deserializador del header de 22 bytes
  - _Test unitario: serializar y deserializar cada campo; verificar byte a byte_
- [x] Implementar enum `FrameType` (0x01–0x0F) y enum `Protocol` (0x00–0xFF)
  - _Test: frame con NetworkID incorrecto descartado silenciosamente_
- [x] Implementar filtrado por NetworkID en recepción (descartar sin descifrar)
  - _Test: 2 redes con misma PSK pero NetworkIDs distintos no se interfieren_
- [x] Implementar serialización de DATA_FRAG con header de fragmento (4B extra)
  - _Test: fragmento correctamente identificado y encolado para reensamblaje_

---

## Fase 3: Criptografía

**Spec:** `openspec/specs/crypto/spec.md`

- [x] Integrar Curve25519 (mbedTLS) para generación de pares efímeros y ECDH
  - _Test: X25519(privA, pubB) == X25519(privB, pubA)_
- [x] Implementar HKDF-SHA256 para NetworkKey, NetworkID y LinkKey
  - _Test: misma PSK produce mismos NetworkKey/NetworkID en 2 nodos; LinkKey depende de MACs_
- [x] Implementar cifrado/descifrado AES-128-GCM con nonce derivado del header
  - _Test: cifrar y descifrar; verificar que campo Protocol está en AD y no puede alterarse_
- [ ] Implementar handshake ECDH completo: HELLO → REPLY → CONFIRM × 2
  - _Test: 2 nodos con PSK correcta negocian LinkKey; nodo con PSK incorrecta falla en CONFIRM_
- [x] Implementar PeerManager con hash table de direccionamiento abierto
  - _Test: insertar 20 peers, buscar por MAC en O(1) amortizado_
- [x] Implementar evicción LRU del PeerManager
  - _Test: forzar presión de heap; verificar que el peer con `lastSeen` más antiguo y `routeCount==0` es evictado_
- [x] Implementar anti-replay por `(peer, seq)` en recepción
  - _Test: reenviar frame con `seq ≤ lastSeqRx` → descartado silenciosamente_

---

## Fase 4: Rotación de clave

**Spec:** `openspec/specs/crypto/spec.md` (§4.3)

- [ ] Implementar timer de rotación de epoch (`setKeyRotationInterval()`)
  - _Test: tras el intervalo, el nodo incrementa epoch y el siguiente frame usa epoch N+1_
- [ ] Implementar `KEY_NACK` y buffer de 1 frame rechazado por peer
  - _Test: emisor recibe KEY_NACK → inicia renegociación → retransmite frame buffereado_
- [ ] Implementar detección de epoch en nodos batería (RTC_DATA_ATTR)
  - _Test: forzar cambio de epoch durante deep sleep; nodo detecta y renegocia al despertar_

---

## Fase 5: Routing básico

**Spec:** `openspec/specs/routing/spec.md`

- [x] Implementar estructura `RouteEntry` y pool estático de 64 entradas
  - _Test unitario: insertar, buscar por IP, actualizar TTL_
- [x] Implementar `ROUTE_ADV` con serialización de entradas (12B/entrada, 18 por frame)
  - _Test: ROUTE_ADV con 18 entradas ocupa exactamente 250 bytes_
- [x] Implementar recepción y actualización de tabla por ROUTE_ADV
  - _Test: 3 nodos A–B–C; C aprende ruta a A vía B tras primer ROUTE_ADV de B_
- [x] Implementar Split Horizon y Poison Reverse
  - _Test: B no anuncia a A las rutas cuyo nextHop es A_
- [x] Implementar Seen-Frame Cache (32 entradas, buffer circular, TTL 10s)
  - _Test: frame recibido 2 veces con mismo (srcMac, seq) → segundo descartado_
- [x] Implementar triggered updates (RA inmediato al detectar cambio de topología)
  - _Test: desconectar peer → RA triggered enviado en < 1s_

---

## Fase 6: Routing completo y ROUTE_WITHDRAW

**Spec:** `openspec/specs/routing/spec.md`

- [x] Implementar evicción de rutas (expiradas → mayor hopCount → menos reciente)
  - _Test: tabla llena con 64 entradas; añadir nueva → la peor entrada es evictada_
- [x] Implementar `ROUTE_WITHDRAW` broadcast
  - _Test: peer desaparece → ROUTE_WITHDRAW enviado → rutas eliminadas en vecinos_
- [ ] Test de reconvergencia: 5 nodos, desconectar nodo central
  - _Test: reconvergencia completa en < 60s (2 intervalos RA de 30s)_

---

## Fase 7: Interfaz IP virtual (netif mesh0)

**Spec:** `openspec/specs/ip-netif/spec.md`

- [x] Crear netif virtual `mesh0` con `esp_netif_new()` y driver custom
  - _Test: `mesh0` aparece en `esp_netif_get_handle_from_ifkey("MESH0")`_
- [x] Implementar path RX: frame DATA descifrado → `esp_netif_receive()`
  - _Test: paquete IPv4 inyectado en `mesh0` recibido por socket UDP en el nodo_
- [x] Implementar path TX: `mesh_netif_output()` → buscar ruta → cifrar → send
  - _Test: socket UDP en nodo A envía a IP de B; B recibe el paquete_
- [ ] Configurar MTU=216 y lwipopts.h (TCP_MSS=176, TCP_WND=704, SACK off)
  - _Test: MSS negociado en handshake TCP = 176_
- [ ] Implementar ping transparente (verificar que esp_ping funciona sobre mesh0)
  - _Test: `ping 10.200.0.x` entre dos nodos; RTT medido < 50ms a 1 hop_

---

## Fase 8: MTU, fragmentación L2 y ARP

**Spec:** `openspec/specs/ip-netif/spec.md`

- [ ] Implementar fragmentación L2 (header 4B extra, timeout reensamblaje 2s)
  - _Test: UDP de 300 bytes; fragmentado en 2 frames L2; reensamblado correctamente_
- [ ] Implementar ARP gratuitous en join
  - _Test: nodo se une → anuncia IP→MAC → vecinos actualizan tabla sin ARP_QUERY_
- [ ] Implementar `ARP_QUERY` broadcast y `ARP_REPLY` unicast
  - _Test: IP desconocida → ARP_QUERY broadcast → ARP_REPLY recibido en < 1s_

---

## Fase 9: DHCP y asignación de IPs

**Spec:** `openspec/specs/ip-netif/spec.md`

- [ ] Implementar tabla estática distribuida (MAC→IP en NVS + distribución via ROUTE_ADV)
  - _Test: nodo recupera su IP de NVS al reiniciar sin hacer DHCP_
- [ ] Implementar servidor DHCP en gateway (lwIP dhcpserver)
  - _Test: nodo sin IP estática obtiene IP del gateway por DHCP_

---

## Fase 10: Onboarding

**Spec:** `openspec/specs/onboarding/spec.md`

- [x] Implementar AP permanente de onboarding con SSID `ENIGMA-<NetworkID>-CH<canal>`
  - _Test: SSID visible por WiFi scanner; contraseña = HMAC(PSK,"onboarding")[:8] hex_
- [ ] Implementar servidor HTTP de provisioning (`GET /provision`)
  - _Test: nodo nuevo conecta al AP, hace GET /provision, recibe JSON correcto_
- [x] Implementar `JOIN_BEACON` broadcast cada 5s
  - _Test: nodo en búsqueda ciega recibe JOIN_BEACON en < 10s si está en el canal correcto_
- [x] Implementar búsqueda ciega de canal (escaneo 1→6→11→resto, 200ms/canal)
  - _Test: nodo configurado se reinicia; encuentra canal en < 30s_

---

## Fase 11: Gateway WiFi

**Spec:** `openspec/specs/gateway/spec.md`

- [x] Implementar modo dual WiFi STA + ESP-NOW (single-chip)
  - _Test: gateway conectado a AP WiFi en canal 6; mesh opera en canal 6_
- [x] Implementar abstracción `MeshUplink` con `NativeWifiUplink`
  - _Test: uplink WiFi conectado; `isConnected()` devuelve true_
- [x] Implementar `ip_forward` entre `mesh0` y `wifi_sta` (o ip_napt según disponibilidad IDF)
  - _Test: nodo mesh hace HTTP GET a servidor en LAN → respuesta correcta_
- [x] Implementar NAT masquerade para tráfico Internet
  - _Test: nodo mesh hace ping a 8.8.8.8 → respuesta recibida_
- [ ] Implementar selección de gateway por métrica y redundancia
  - _Test: 2 gateways activos; nodo elige el de mejor métrica; si cae, migra al otro en < 60s_

---

## Fase 12: Web UI y Prometheus

**Spec:** `openspec/specs/gateway/spec.md`

- [x] Implementar servidor `esp_http_server` con HTTP Digest Auth
  - _Test: GET `/` con credenciales incorrectas devuelve 401; correctas devuelve 200_
- [x] Implementar dashboard HTML mínimo (topología, nodos, rutas)
  - _Test: navegador carga `/` correctamente_
- [x] Implementar endpoints JSON: `/api/v1/status`, `/nodes`, `/routes`, `/peers`
  - _Test: JSON válido, campos correctos_
- [x] Implementar endpoint `/metrics` en formato Prometheus
  - _Test: `curl http://<gw>/metrics` devuelve texto con métricas listadas en el spec_

---

## Fase 13: Nodos batería

**Spec:** `openspec/specs/battery-nodes/spec.md`

- [x] Implementar ciclo WAKE → TX UPLINK → RX1 → RX2 → DEEP SLEEP
  - _Test: nodo cicla cada 60s (configurable); consume < 1mA promedio_
- [x] Implementar buffer downlink en Parent (FIFO, 5 msgs × 200B por hijo)
  - _Test: 5 mensajes en buffer; nodo los recibe todos en ventanas RX tras UPLINK_
- [ ] Implementar sincronización de reloj (timestamp del Parent en respuesta UPLINK)
  - _Test: `getMeshTime()` en nodo batería devuelve tiempo válido tras primer UPLINK_
- [x] Implementar lista de 3 candidatos a Parent en NVS
  - _Test: Parent primario desaparece; nodo recupera conexión vía candidato 2 sin re-join_
- [ ] Verificar que nodo batería NO actúa como relay
  - _Test: frame de terceros recibido por nodo batería → descartado (no retransmitido)_

---

## Fase 14: ESP8266 (librería derivada)

**Spec:** `openspec/specs/esp8266/spec.md`

- [ ] Implementar `MeshNode8266` con protocolo Proxy MQTT
  - _Test: ESP8266 compila la librería sin errores_
- [ ] Implementar `PROXY_DISCOVERY` / `PROXY_OFFER` (selección de proxy por RSSI)
  - _Test: ESP8266 selecciona el proxy ESP32 de mejor RSSI_
- [ ] Implementar `PROXY_PUBLISH` y `PROXY_MESSAGE` (broker → ESP8266)
  - _Test: ESP8266 publica en topic; mensaje llega al broker MQTT_
- [ ] Implementar distribución de broker vía provisioning y JOIN_BEACON
  - _Test: cambio de broker en gateway; ESP8266 actualiza en el siguiente JOIN_BEACON_

---

## Fase 15: Descubrimiento de servicios

**Spec:** `openspec/specs/service-discovery/spec.md`

- [x] Implementar `SERVICE_QUERY` / `SERVICE_REPLY` sobre mesh
  - _Test: nodo consulta MQTT broker; gateway responde con IP:puerto en < 1s_
- [x] Implementar registros de servicios en ROUTE_ADV (campo opcional)
  - _Test: servicio anunciado en RA visible en tabla de servicios local tras 30s_
- [x] Implementar bridge mDNS en gateway (republica servicios mesh hacia WiFi)
  - _Test: `avahi-browse` en dispositivo LAN descubre `_mqtt._tcp` del broker mesh_

---

## Fase 16: API pública y ejemplos

**Spec:** `openspec/specs/public-api/spec.md`

- [x] Finalizar `MeshNetwork.h` con API completa según spec
  - _Test: compilación sin warnings en Arduino IDE y PlatformIO_
- [ ] Implementar `getClient()` como wrapper `WiFiClient` sobre socket lwIP en mesh0
  - _Test: `PubSubClient` con `getClient()` conecta y publica en broker MQTT en LAN_
- [x] Crear ejemplo `BasicNode` (Arduino)
  - _Test: flashear en ESP32; nodo aparece en Web UI del gateway_
- [x] Crear ejemplo `GatewaySingleChip` (Arduino)
  - _Test: flashear en ESP32; AP onboarding visible; nodo BasicNode se conecta_
- [x] Crear ejemplo `BatteryNode` (Arduino)
  - _Test: nodo cicla cada 60s; consumo medido_
- [x] Crear ejemplo `gateway_hosted` (IDF nativo)
  - _Test: compila con `idf.py build`; gateway funcional con placa slave ESP-Hosted_
- [x] Crear `library.json` y verificar compatibilidad con Arduino Library Manager
  - _Test: `pio lib install` desde el repositorio funciona_
