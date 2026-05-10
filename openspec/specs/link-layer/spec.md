# Spec: Link Layer — Frame Format

**Reference:** §4.1 of EnigmaNG Specs v2.md

## Purpose

Define the binary frame format that carries all EnigmaNG messages over ESP-NOW (250 bytes maximum).

## Frame structure

```
[Magic:      2 bytes]   // 0x454E ("EN")
[Version:    1 byte]    // 0x01
[Network ID: 2 bytes]   // HKDF(PSK, "netid", len=2)
[Frame Type: 1 byte]    // Enum FrameType
[Protocol:   1 byte]    // L3 protocol identifier (EtherType-like)
[Epoch:      1 byte]    // Link key epoch (0–255, wrap-around)
[Src MAC:    6 bytes]   // Final sender MAC (end-to-end)
[Dst MAC:    6 bytes]   // Final receiver MAC (FF:FF:FF:FF:FF:FF = broadcast)
[Sequence:   2 bytes]   // Per-link counter, anti-replay + nonce component
[Payload:    variable]  // AES-128-GCM encrypted (max 216 bytes)
[Tag:        12 bytes]  // GCM Authentication Tag
```

**Fixed overhead:** 22B header + 12B tag = **34 bytes**
**Available payload:** 250 - 34 = **216 bytes**

## Frame Types

| Value | Name | Encrypted with |
|-------|------|----------------|
| 0x01 | `JOIN_BEACON` | Network Key |
| 0x02 | `KEY_EXCH_HELLO` | Plain (auth with Network Key tag) |
| 0x03 | `KEY_EXCH_REPLY` | Plain (auth with Network Key tag) |
| 0x04 | `KEY_EXCH_CONFIRM` | Link Key |
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
| 0x0F | `KEY_NACK` | Network Key |

## Protocol field (L3 Multiplexing)

| Value | Identifier | Description |
|-------|------------|-------------|
| `0x00` | `PROTO_MESH_INTERNAL` | Mesh internal control (default for non-DATA frames) |
| `0x01` | `PROTO_IPV4` | Encapsulated IPv4 packet (main protocol v1.0) |
| `0x02` | `PROTO_IPV6` | Reserved for v1.x |
| `0x10` | `PROTO_ESPHOME` | Native ESPHome protocol (reserved §16) |
| `0x11` | `PROTO_COAP` | CoAP directly over L2 (future) |
| `0x12` | `PROTO_MQTT_SN` | MQTT-SN over L2 (future) |
| `0x20`–`0xEF` | `PROTO_USER_*` | User proprietary protocols |
| `0xF0`–`0xFF` | Reserved | Experimental/diagnostic |

## AES-GCM Nonce (not transmitted)

```
Nonce (12 bytes) = Epoch(1B) || Sequence(2B) || SrcMAC(6B) || 0x000000(3B)
```

The `Protocol` field is part of the AES-GCM Additional Data (AD) → authenticated but not encrypted; it cannot be altered without invalidating the tag.

## Size footprint by frame type

| Frame | Total |
|-------|-------|
| KEY_EXCH_HELLO / REPLY | 98B ✓ |
| KEY_EXCH_CONFIRM | 50B ✓ |
| DATA (payload 176B) | 210B ✓ |
| ROUTE_ADV (18 IPv4 entries) | 250B ✓ (exact limit) |
| DATA_FRAG | ≤238B ✓ |

## Acceptance criteria

- Unit test: serialize/deserialize each frame type. Verify fields match byte-for-byte.
- Test: frame with incorrect NetworkID is silently discarded without attempting decryption.
- Test: Protocol field in control frames = 0x00 always.
