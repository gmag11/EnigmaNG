# Spec: Criptografía y Gestión de Claves

**Referencia:** §4.2, §4.3, §4.4 de EnigmaNG Specs v2.md

## Propósito

Proporcionar confidencialidad, autenticidad e integridad a todos los frames de la mesh mediante dos anillos de cifrado: Network Key (broadcast) y Link Key (unicast, por peer).

## Primitivas criptográficas

| Primitiva | Algoritmo | Implementación |
|-----------|-----------|----------------|
| ECDH | Curve25519 | mbedTLS (IDF built-in) |
| KDF | HKDF-SHA256 (RFC 5869) | mbedTLS |
| Cifrado unicast | AES-128-GCM | mbedTLS + HW aceleración ESP32 |
| Cifrado broadcast | AES-128-GCM | Misma clave para todos los miembros |
| CSPRNG | esp_random() | Hardware RNG del ESP32 |

## Clave de Red (Network Key)

```
NetworkKey = HKDF-SHA256(PSK, salt="enigmang-net-v1", info="broadcast", len=16)
NetworkID  = HKDF-SHA256(PSK, salt="enigmang-net-v1", info="netid",     len=2)
```

Todos los nodos con la misma PSK comparten NetworkKey y NetworkID. El NetworkID en el header permite descartar frames de otras redes sin descifrar.

## Handshake ECDH (Link Key) — Station-to-Station simplificado

```
A → B:  KEY_EXCH_HELLO { pubA_efimera(32B), nonceA(32B) }
B → A:  KEY_EXCH_REPLY  { pubB_efimera(32B), nonceB(32B) }

SharedSecret = X25519(privA_efimera, pubB_efimera)
            = X25519(privB_efimera, pubA_efimera)

LinkKey = HKDF-SHA256(
            IKM  = SharedSecret,
            salt = PSK,
            info = "link" || macA || macB,
            len  = 16
          )

A → B:  KEY_EXCH_CONFIRM { AES-GCM[LinkKey](challenge = nonceA XOR nonceB) }
B → A:  KEY_EXCH_CONFIRM { AES-GCM[LinkKey](challenge = nonceB XOR nonceA) }
```

**Propiedades:**
- Autenticación PSK: salt incluye PSK; PSK incorrecta → LinkKey diferente → challenge falla.
- Forward secrecy: pares efímeros destruidos tras handshake.
- Anti-replay: nonceA y nonceB son 32B aleatorios del CSPRNG.

## Rotación de Clave — Epoch + Rechazo + Renegociación

- Intervalo por defecto: 86.400s (24h). Configurable con `setKeyRotationInterval()`.
- **Secuencia:**
  1. Receptor detecta `epoch N+1` en frame de peer conocido con `epoch N`.
  2. Envía `KEY_NACK { epoch_esperado: N+1 }`.
  3. Buffer del frame rechazado (max 1 por peer).
  4. Emisor recibe KEY_NACK → inicia KEY_EXCH_HELLO inmediato.
  5. Tras KEY_EXCH_CONFIRM exitoso → retransmite el frame en buffer → purga buffer.
- **Nodos batería:** comparan epoch en RTC memory con epoch del primer frame recibido al despertar.

## PeerManager

```cpp
struct PeerEntry {
    uint8_t  mac[6];           // 6B
    uint8_t  linkKey[16];      // 16B — AES-128
    uint8_t  epoch;            // 1B
    int8_t   avgRssi;          // 1B — EWMA α=0.3
    bool     canRelay;         // 1B
    bool     isBattery;        // 1B
    uint32_t lastSeen;         // 4B — millis()
    uint32_t lastSeqRx;        // 4B — anti-replay
    uint16_t lastSeqTx;        // 2B
    uint16_t routeCount;       // 2B
};
// sizeof(PeerEntry) ≈ 36 bytes
```

- **Almacenamiento:** Hash table de direccionamiento abierto. Tamaño inicial: 16 slots.
- **Evicción LRU:** Si `heap_free < PEER_HEAP_LOW_WATERMARK` (20KB), evictar peer con `lastSeen` más antiguo y `routeCount == 0`.
- **Cleanup periódico:** Cada 60s, eliminar peers con `lastSeen > PEER_TIMEOUT` (3600s).
- **Anti-replay:** Rechazar frame con `seq ≤ lastSeqRx` del mismo peer. Sequence se reinicia en cada renegociación.

## Almacenamiento de claves

- ESP32: NVS (wear leveling nativo). Clave: `"enigmang/peer/<mac_hex>"`.
- ESP8266: EEPROM (estructura compacta, offset fijo por slot).

## Criterio de aceptación

- Test: 2 nodos negocian LinkKey. Tercer nodo con PSK incorrecta no puede descifrar.
- Test: forzar rotación de epoch. Verificar KEY_NACK + renegociación + retransmisión en < 500ms.
- Test: frame con `seq ≤ lastSeqRx` es descartado silenciosamente.
- Test: evicción LRU cuando heap < 20KB.
