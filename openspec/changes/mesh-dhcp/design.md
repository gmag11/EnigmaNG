# Diseño: Servidor DHCP en Gateway (mesh-dhcp)

**Referencia:** `openspec/specs/ip-netif/spec.md` §Asignación de IPs

## Contexto actual

`Gateway::startDHCPServer(poolStart, poolEnd)` en `src/Gateway.cpp` es un stub: solo asigna `_dhcpRunning = true` y retorna `true`. No llama a ninguna API lwIP real.

## Decisión de implementación

### API lwIP dhcpserver (ESP-IDF 5.x)

Arduino Core ESP32 3.3.8 expone la cabecera `dhcpserver/dhcpserver.h` con:

```c
// Tipos relevantes
typedef struct dhcps_s* dhcps_handle_t;

typedef struct {
    ip4_addr_t start;
    ip4_addr_t end;
} dhcps_lease_t;

// Funciones clave
dhcps_handle_t dhcps_new(tcpip_adapter_ip_info_t* ip_info);
esp_err_t dhcps_start(dhcps_handle_t dhcps, struct netif* netif, ip4_addr_t ip);
esp_err_t dhcps_stop(dhcps_handle_t dhcps, struct netif* netif);
esp_err_t dhcps_set_option_info(dhcps_handle_t dhcps, uint8_t op_id, void* opt_info, uint32_t opt_len);
void dhcps_delete(dhcps_handle_t dhcps);
```

### Integración con `mesh0`

El servidor DHCP debe arrancar **sobre el netif `mesh0`** (no sobre `wifi_sta`), de forma que los nodos mesh reciban IPs del pool cuando no tienen asignación estática.

Flujo en `Gateway::startDHCPServer()`:

1. Obtener el `netif*` de lwIP asociado a `mesh0` via `esp_netif_get_netif_impl()`.
2. Construir `tcpip_adapter_ip_info_t` con la IP del gateway (`10.200.0.1`) y máscara (`255.255.0.0`).
3. Crear instancia con `dhcps_new()`.
4. Configurar el pool de leases con `dhcps_set_option_info(DHCPS_OP_LEASE_POOL, ...)`.
5. Arrancar con `dhcps_start()` pasando el `netif` de `mesh0`.
6. Guardar el handle en `_dhcpsHandle` para poder parar el servidor (`stop()`).

### Pool de direcciones por defecto

- Gateway: `10.200.0.1` (ya configurado en `mesh0`).
- Pool: `10.200.0.2` – `10.200.0.254`.
- Lease time: 24 horas (valor por defecto de lwIP dhcpserver).

### Ciclo de vida

| Evento | Acción |
|--------|--------|
| `Gateway::begin()` | Llama a `startDHCPServer(10.200.0.2, 10.200.0.254)` automáticamente tras `enableNAT()` |
| `Gateway::stop()` | Llama a `dhcps_stop()` + `dhcps_delete()` si `_dhcpsRunning` |

### Mock para tests unitarios

El stub en `test/mocks/dhcpserver/dhcpserver.h` debe declarar los tipos y funciones con implementaciones vacías/no-op para que los tests que incluyen `Gateway.h` compilen sin errores.

## Cambios de archivos previstos

| Archivo | Cambio |
|---------|--------|
| `src/Gateway.cpp` | Implementación real de `startDHCPServer()` + llamada en `begin()` + cleanup en `stop()` |
| `src/Gateway.h` | Añadir campo `dhcps_handle_t _dhcpsHandle = nullptr` |
| `test/mocks/dhcpserver/dhcpserver.h` | Declarar tipos y stubs de las funciones reales |
