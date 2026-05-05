# Spec: Capa de Enlace — Frame Format

**Referencia:** §4.1 de EnigmaNG Specs v2.md

## Propósito

Definir el formato de frame binario que transporta todos los mensajes de EnigmaNG sobre ESP-NOW (250 bytes máximo).

## Estructura del frame

```
[Magic:      2 bytes]   // 0x454E ("EN")
[Version:    1 byte]    // 0x01
[Network ID: 2 bytes]   // HKDF(PSK, "netid", len=2)
[Frame Type: 1 byte]    // Enum FrameType
[Protocol:   1 byte]    // Identificador protocolo L3 (EtherType-like)
[Epoch:      1 byte]    // Epoch clave de enlace (0–255, wrap-around)
[Src MAC:    6 bytes]   // MAC emisor final (end-to-end)
[Dst MAC:    6 bytes]   // MAC receptor final (FF:FF:FF:FF:FF:FF = broadcast)
[Sequence:   2 bytes]   // Contador por enlace, anti-replay + componente nonce
[Payload:    variable]  // Cifrado AES-128-GCM (máx 216 bytes)
[Tag:        12 bytes]  // GCM Authentication Tag
```

**Overhead fijo:** 22B header + 12B tag = **34 bytes**  
**Payload disponible:** 250 - 34 = **216 bytes**

## Frame Types

| Valor | Nombre | Cifrado con |
|-------|--------|------------|
| 0x01 | `JOIN_BEACON` | Network Key |
| 0x02 | `KEY_EXCH_HELLO` | Plano (auth con Network Key tag) |
| 0x03 | `KEY_EXCH_REPLY` | Plano (auth con Network Key tag) |
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

## Campo Protocol (L3 Multiplexing)

| Valor | Identificador | Descripción |
|-------|--------------|-------------|
| `0x00` | `PROTO_MESH_INTERNAL` | Control interno mesh (default en frames no-DATA) |
| `0x01` | `PROTO_IPV4` | Paquete IPv4 encapsulado (protocolo principal v1.0) |
| `0x02` | `PROTO_IPV6` | Reservado para v1.x |
| `0x10` | `PROTO_ESPHOME` | Protocolo nativo ESPHome (reservado §16) |
| `0x11` | `PROTO_COAP` | CoAP directo sobre L2 (futuro) |
| `0x12` | `PROTO_MQTT_SN` | MQTT-SN sobre L2 (futuro) |
| `0x20`–`0xEF` | `PROTO_USER_*` | Protocolos propietarios usuario |
| `0xF0`–`0xFF` | Reservado | Experimental/diagnóstico |

## Nonce AES-GCM (no transmitido)

```
Nonce (12 bytes) = Epoch(1B) || Sequence(2B) || SrcMAC(6B) || 0x000000(3B)
```

El campo `Protocol` forma parte del Additional Data (AD) de AES-GCM → autenticado pero no cifrado, no puede ser alterado sin invalidar el tag.

## Huella por tipo de frame

| Frame | Total |
|-------|-------|
| KEY_EXCH_HELLO / REPLY | 98B ✓ |
| KEY_EXCH_CONFIRM | 50B ✓ |
| DATA (payload 176B) | 210B ✓ |
| ROUTE_ADV (18 entradas IPv4) | 250B ✓ (límite exacto) |
| DATA_FRAG | ≤238B ✓ |

## Criterio de aceptación

- Test unitario: serializar/deserializar cada tipo de frame. Verificar que los campos coinciden byte a byte.
- Test: frame con NetworkID incorrecto es descartado silenciosamente sin intento de descifrado.
- Test: campo Protocol en frames de control = 0x00 siempre.
