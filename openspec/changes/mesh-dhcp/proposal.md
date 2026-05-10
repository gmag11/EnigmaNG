# Propuesta: Servidor DHCP en Gateway (mesh-dhcp)

## ¿Qué se va a construir?

Implementar el servidor DHCP en el gateway EnigmaNG para que los nodos mesh puedan obtener una dirección IP dinámica sin necesidad de configuración estática en NVS.

## ¿Por qué?

La especificación `ip-netif` define tres modos de asignación de IP para los nodos:
- **Estático distribuido** (ya implementado): tabla MAC→IP en NVS, distribuida via ROUTE_ADV.
- **DHCP** (pendiente): servidor en el gateway, usando lwIP `dhcpserver`.
- **IP fija manual** (ya implementado): `begin(psk, IPAddress(...))`.

Sin el servidor DHCP, los nodos "plug-and-play" que no tienen IP preasignada no pueden unirse a la mesh de forma automática. La función `Gateway::startDHCPServer()` existe en el código pero es un stub vacío.

## Alcance

### Incluye

- Implementación real de `Gateway::startDHCPServer(poolStart, poolEnd)` usando la API lwIP `dhcpserver` (disponible en ESP-IDF 5.x vía Arduino Core ESP32 3.3.8).
- Integración en `Gateway::begin()` para activar el servidor automáticamente sobre la interfaz `mesh0`.
- Configuración del pool de direcciones por defecto: `10.200.0.2` – `10.200.0.254` (subred `10.200.0.0/16`).
- Actualización del stub en `test/mocks/dhcpserver/dhcpserver.h` para que soporte los tipos y funciones reales.
- Test unitario verificando que el servidor se inicia y sirve una IP a un nodo sin IP estática.

### No incluye

- DHCP relay (no necesario: todos los nodos de la mesh ven al gateway directamente vía L3).
- Opción 43/60 u opciones DHCP extendidas.
- Soporte ESP8266 (no tiene stack IP completo; usa Proxy MQTT).
