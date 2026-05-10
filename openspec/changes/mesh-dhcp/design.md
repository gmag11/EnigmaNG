# Design: DHCP Server on Gateway (mesh-dhcp)

**Reference:** `openspec/specs/ip-netif/spec.md` §IP assignment

## Current context

`Gateway::startDHCPServer(poolStart, poolEnd)` in `src/Gateway.cpp` is a stub: it only sets `_dhcpRunning = true` and returns `true`. It does not call any real lwIP API.

## Implementation decision

### lwIP dhcpserver API (ESP-IDF 5.x)

Arduino Core ESP32 3.3.8 exposes the header `dhcpserver/dhcpserver.h` with:

```c
// Relevant types
typedef struct dhcps_s* dhcps_handle_t;

typedef struct {
    ip4_addr_t start;
    ip4_addr_t end;
} dhcps_lease_t;

// Key functions
dhcps_handle_t dhcps_new(tcpip_adapter_ip_info_t* ip_info);
esp_err_t dhcps_start(dhcps_handle_t dhcps, struct netif* netif, ip4_addr_t ip);
esp_err_t dhcps_stop(dhcps_handle_t dhcps, struct netif* netif);
esp_err_t dhcps_set_option_info(dhcps_handle_t dhcps, uint8_t op_id, void* opt_info, uint32_t opt_len);
void dhcps_delete(dhcps_handle_t dhcps);
```

### Integration with `mesh0`

The DHCP server must run **on the `mesh0` netif** (not on `wifi_sta`), so mesh nodes obtain IPs from the pool when they have no static assignment.

Flow in `Gateway::startDHCPServer()`:

1. Obtain the `netif*` for `mesh0` via `esp_netif_get_netif_impl()`.
2. Build `tcpip_adapter_ip_info_t` with the gateway IP (`10.200.0.1`) and mask (`255.255.0.0`).
3. Create instance with `dhcps_new()`.
4. Configure the lease pool with `dhcps_set_option_info(DHCPS_OP_LEASE_POOL, ...)`.
5. Start with `dhcps_start()` passing the `mesh0` `netif`.
6. Save the handle in `_dhcpsHandle` to stop the server later (`stop()`).

### Default address pool

- Gateway: `10.200.0.1` (already configured on `mesh0`).
- Pool: `10.200.0.2` – `10.200.0.254`.
- Lease time: 24 hours (default lwIP dhcpserver value).

### Lifecycle

| Event | Action |
|--------|--------|
| `Gateway::begin()` | Calls `startDHCPServer(10.200.0.2, 10.200.0.254)` automatically after `enableNAT()` |
| `Gateway::stop()` | Calls `dhcps_stop()` + `dhcps_delete()` if `_dhcpsRunning` |

### Mock for unit tests

The stub in `test/mocks/dhcpserver/dhcpserver.h` should declare the types and functions with empty/no-op implementations so tests that include `Gateway.h` compile without errors.

## Files to change

| File | Change |
|------|--------|
| `src/Gateway.cpp` | Real implementation of `startDHCPServer()` + call in `begin()` + cleanup in `stop()` |
| `src/Gateway.h` | Add field `dhcps_handle_t _dhcpsHandle = nullptr` |
| `test/mocks/dhcpserver/dhcpserver.h` | Declare types and stubs for the real functions |
