# Spec: IP Layer — Virtual netif, MTU, ARP, DHCP, ICMP

**Reference:** §8 of EnigmaNG Specs v2.md

## Purpose

Expose the ESP-NOW mesh as a standard IP network interface (`mesh0`) via a virtual lwIP netif, allowing any TCP/UDP application to run over the mesh without modification.

## Virtual interface `mesh0`

- Created with `esp_netif_new()` and a custom driver (`ESP_NETIF_ID_CUSTOM`).
- **RX path:** `DATA` frame decrypted → `esp_netif_receive()` → lwIP.
- **TX path:** `mesh_netif_output()` → lookup route → encrypt → `sendUnicast/Broadcast()`.

## MTU and TCP configuration

```
ESP-NOW payload:   250 bytes
L2 header:         22 bytes
GCM tag:           12 bytes
──────────────────────────────
MTU (mesh0):       216 bytes
TCP MSS:           176 bytes  (216 - 20 IP - 20 TCP)
UDP max payload:   188 bytes  (216 - 20 IP - 8 UDP)
```

**Recommended `lwipopts.h`:**

```c
#define TCP_MSS              176
#define TCP_WND              (4 * TCP_MSS)   // 704 bytes
#define TCP_SND_BUF          (4 * TCP_MSS)
#define LWIP_TCP_SACK_OUT    0
```

## L2 fragmentation

For IP payloads > 216B (rare — TCP MSS prevents it for TCP):

```
[FragID: 2B][FragOffset: 1B][FragFlags: 1B] = 4 bytes overhead
```

- Reassembly timeout: 2s.
- Maximum 4 fragments per packet.

## IP assignment

| Mode | Scenario | Mechanism |
|------|----------|-----------|
| Distributed static (default) | Battery nodes, fixed installations | MAC→IP in NVS; distributed table via ROUTE_ADV |
| DHCP | Nodes with dynamic IP | DHCP server on gateway (lwIP dhcpserver) |
| Manual static IP | Explicit configuration | `begin(psk, IPAddress(...))` |

**Subnet:** `10.200.0.0/16` (configurable). Gateway: first assignable IP.

## ARP

- The unified routing table (§6) resolves IP→MAC locally → no ARP traffic for known destinations.
- `ARP_QUERY` broadcast only if IP not in table.
- Gratuitous `ARP_REPLY` is sent on each join (announces IP→MAC when joining the mesh).

## ICMP (Ping)

ICMP is fully transparent — **no changes required at the mesh layer.**

- ICMP is IPv4 protocol 1 → travels as `PROTO_IPV4 (0x01)` in the mesh header.
- lwIP replies automatically to echo requests on any registered netif (including `mesh0`).
- Relays forward the DATA frame like any other; they don’t inspect inner IP protocol.

**Typical ping size:**

```
ICMP echo request: 20B IP + 8B ICMP + 32B data = 60B payload
Frame total:       22B header + 60B payload + 12B tag = 94 bytes
```

**`esp_ping` API works without extra configuration:**

```c
esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
config.target_addr.u_addr.ip4.addr = ipaddr_addr("10.200.0.5");
config.target_addr.type = IPADDR_TYPE_V4;
// lwIP selects mesh0 automatically for 10.200.0.0/16
esp_ping_new_session(&config, &cbs, &ping);
esp_ping_start(ping);
```

## Acceptance criteria

- Test: `ping` between two nodes with static IPs. Measure RTT.
- Test: UDP 200 bytes (requires L2 fragmentation). Reassembly correct at destination.
- Test: TCP MQTT QoS 0 publish functional between node and broker on LAN.
- Test: `ping 8.8.8.8` from mesh node via gateway (mesh → gateway → NAT → Internet).
- Test: TCP MQTT publish from mesh node to LAN broker (mesh → gateway → NAT → LAN). Broker sees gateway WiFi IP as source.
