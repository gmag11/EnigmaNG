# Propuesta: EnigmaNG v1.0 — Implementación completa

## ¿Qué se va a construir?

EnigmaNG es una librería Arduino/IDF para ESP32, compatible con PlatformIO, que crea una red mesh segura sobre ESP-NOW con transparencia IP completa. Los nodos de la mesh pueden usar TCP/UDP estándar (MQTT, HTTP, etc.) como si estuviesen conectados a WiFi, sin modificar las librerías de aplicación.

## ¿Por qué?

EnigmaIOT (la librería anterior del autor) no tiene transparencia IP: las aplicaciones deben usar la API propietaria de EnigmaIOT para enviar datos. EnigmaNG elimina esa limitación añadiendo un netif virtual lwIP (`mesh0`) que hace que la mesh sea completamente transparente a las aplicaciones TCP/IP estándar.

## Alcance de v1.0

### Incluye

- **Capa física:** Wrapper sobre QuickESPNow con EWMA de RSSI y gestión de canal.
- **Capa de enlace:** Frame de 22B header + 12B tag. Campo `Protocol` (EtherType-like). 15 tipos de frame.
- **Criptografía:** Curve25519 ECDH + HKDF-SHA256 + AES-128-GCM. Rotación de clave via Epoch + KEY_NACK. PeerManager con hash table.
- **Routing:** DVR proactivo (RIPv2-like). Seen-Frame Cache. Split Horizon + Poison Reverse. Hasta ~100 nodos.
- **IP transparente:** netif virtual `mesh0` via esp_netif. MTU 216 bytes. TCP MSS 176 bytes. ARP, DHCP, ICMP.
- **Onboarding:** AP permanente en gateway. HTTP provisioning. Búsqueda ciega de canal como fallback.
- **Nodos batería:** Ciclo LoRaWAN Clase A. Buffer downlink en Parent. 3 candidatos a Parent en NVS.
- **Gateway:** WiFi STA+AP dual. Routing LAN + NAT Internet. Web UI (esp_http_server + HTTP Digest Auth). Prometheus `/metrics`.
- **ESP8266:** Librería derivada. Solo Proxy MQTT vía ESP32 relay.
- **Descubrimiento de servicios:** Protocolo propio ligero + bridge mDNS en gateway.
- **API pública:** `MeshNetwork` compatible con PubSubClient, HTTPClient. `getClient()` devuelve `WiFiClient`.

### No incluye (fuera de alcance)

- Autenticación por certificados (postpuesto a v1.1 — §13).
- IPv6 (reservado para v1.x — campo Protocol `0x02` reservado).
- Integración ESPHome (reservado para v1.x — §16).
- Gateway dual-board ESP-Hosted (opción avanzada, documentada en §9.5 pero como ejemplo IDF separado, no en la librería principal).
- OTA sobre la mesh (postpuesto).
- Más de ~100 nodos (límite práctico de ESP-NOW, no diseñado para superar eso en v1.0).

## Plataformas objetivo

| Plataforma | Soporte | Notas |
|-----------|---------|-------|
| Arduino Core ESP32 3.3.8 (IDF 5.5.4) | ✅ Completo | Plataforma primaria |
| ESP8266 Arduino Core | ✅ Parcial | Solo `MeshNode8266` (Proxy MQTT) |
| IDF nativo (CMake) | ✅ vía idf_component/ | Necesario para gateway dual-board |

## Estimación de fases

12 fases de desarrollo definidas en §15 del spec. Ver `tasks.md` para el desglose completo.

## Riesgos principales

| Riesgo | Mitigación |
|--------|-----------|
| Compatibilidad QuickESPNow con IDF 5.5.4 | Verificar en Fase 0 antes de construir nada encima |
| `ip_napt` de lwIP no disponible en IDF 5.5.4 | Implementación custom via raw socket (ya previsto) |
| Heap insuficiente en ESP32 con muchos peers | Evicción LRU + `PEER_HEAP_LOW_WATERMARK` ya diseñado |
| Fragmentación L2 compleja | Usada solo para payloads > 216B; TCP MSS evita esto para TCP |
