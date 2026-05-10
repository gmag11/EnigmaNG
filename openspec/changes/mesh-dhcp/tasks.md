# Tareas: mesh-dhcp — Servidor DHCP en Gateway

## Progreso: 0/3 tareas completadas

---

## Fase 1: Mock y tipos

**Spec:** `openspec/specs/ip-netif/spec.md` §Asignación de IPs

- [ ] Actualizar `test/mocks/dhcpserver/dhcpserver.h` con los tipos y stubs reales de la API lwIP dhcpserver
  - _Test: `pio test -e native` compila sin errores tras el cambio_

---

## Fase 2: Implementación

**Spec:** `openspec/specs/ip-netif/spec.md` §Asignación de IPs

- [ ] Implementar `Gateway::startDHCPServer(poolStart, poolEnd)` usando `dhcps_new()` / `dhcps_start()` sobre el netif `mesh0`; añadir `dhcps_handle_t _dhcpsHandle` en `Gateway.h`; invocar en `Gateway::begin()` y limpiar en `Gateway::stop()`
  - _Test: `pio run -e esp32` compila sin warnings; en hardware: nodo sin IP estática obtiene IP del gateway por DHCP en < 5s_

---

## Fase 3: Integración en API pública

- [ ] Verificar que `GatewaySingleChip` y `gateway_hosted` ejemplos arrancan el DHCP server correctamente; documentar el comportamiento en los comentarios de `MeshNetwork.h`
  - _Test: ambos ejemplos compilan; comentario `// DHCP: nodos sin IP estática reciben IP del pool` visible en API_
