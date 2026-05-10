## MODIFIED Requirements

### Requirement: Protocol field (L3 Multiplexing)
The frame `Protocol` field acts as an L3 protocol identifier (equivalent to EtherType). The updated table is:

| Value | Identifier | Description |
|-------|------------|-------------|
| `0x00` | `PROTO_MESH_INTERNAL` | Mesh internal control (default for non-DATA frames) |
| `0x01` | `PROTO_IPV4` | Encapsulated IPv4 packet (main protocol) |
| `0x02` | `PROTO_IPV6` | Encapsulated IPv6 packet (**active from this version**) |
| `0x10` | `PROTO_ESPHOME` | Native ESPHome protocol (reserved §16) |
| `0x11` | `PROTO_COAP` | CoAP direct over L2 (future) |
| `0x12` | `PROTO_MQTT_SN` | MQTT-SN over L2 (future) |
| `0x20`–`0xEF` | `PROTO_USER_*` | User proprietary protocols |
| `0xF0`–`0xFF` | Reserved | Experimental/diagnostic |

`PROTO_IPV6 (0x02)` goes from "Reserved for v1.x" to **active**. Nodes that receive a frame with `Protocol = 0x02` and do not have IPv6 enabled (`ENIGMANG_IPV6_ENABLED` not defined) SHALL silently discard the frame.

#### Scenario: IPv6 frame received by node with IPv6 enabled
- **WHEN** a node receives a `DATA` frame with `Protocol = 0x02`
- **THEN** `LinkLayer` delivers the payload to lwIP via `esp_netif_receive()` for IPv6 processing

#### Scenario: IPv6 frame received by node without IPv6 enabled
- **WHEN** a node without `ENIGMANG_IPV6_ENABLED` receives a `DATA` frame with `Protocol = 0x02`
- **THEN** the frame is discarded without error (silent drop)
- **THEN** no error frame is generated and IPv4 traffic is not affected

## ADDED Requirements

### Requirement: Typical IPv6 frame size
The link layer SHALL support `PROTO_IPV6` frames with up to 216B of payload. Typical sizes documented:

```
ICMPv6 echo (ping6, 32B data):  40B IPv6 + 8B ICMPv6 + 32B = 80B payload  → Total frame: 114B ✓
Router Solicitation:            40B IPv6 + 8B ICMPv6 = 48B payload          → Total frame: 82B  ✓
Router Advertisement (minimum): 40B IPv6 + 16B ICMPv6 base = 56B payload    → Total frame: 90B  ✓
TCP SYN IPv6 (no data):         40B IPv6 + 20B TCP = 60B payload             → Total frame: 94B  ✓
Typical UDP CoAP (50B data):    40B IPv6 + 8B UDP + 50B = 98B payload        → Total frame: 132B ✓
```

#### Scenario: Maximum-size IPv6 frame
- **WHEN** lwIP generates an IPv6 packet with 216B of payload (the maximum mesh MTU)
- **THEN** the total frame is 22B + 216B + 12B = 250B, within the ESP-NOW limit
- **THEN** the frame is sent correctly without truncation