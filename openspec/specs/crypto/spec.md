# Spec: Cryptography and Key Management

**Reference:** §4.2, §4.3, §4.4 of EnigmaNG Specs v2.md

## Purpose

Provide confidentiality, authenticity and integrity for all mesh frames using two encryption rings: Network Key (broadcast) and Link Key (unicast, per-peer).

## Cryptographic primitives

| Primitive | Algorithm | Implementation |
|-----------|-----------|----------------|
| ECDH | Curve25519 | mbedTLS (IDF built-in) |
| KDF | HKDF-SHA256 (RFC 5869) | mbedTLS |
| Unicast encryption | AES-128-GCM | mbedTLS + ESP32 HW acceleration |
| Broadcast encryption | AES-128-GCM | Same key for all members |
| CSPRNG | esp_random() | ESP32 hardware RNG |

## Network Key

```
NetworkKey = HKDF-SHA256(PSK, salt="enigmang-net-v1", info="broadcast", len=16)
NetworkID  = HKDF-SHA256(PSK, salt="enigmang-net-v1", info="netid",     len=2)
```

All nodes sharing the same PSK also share NetworkKey and NetworkID. The NetworkID in the header allows discarding frames from other networks without attempting decryption.

## ECDH Handshake (Link Key) — simplified Station-to-Station

```
A → B:  KEY_EXCH_HELLO { pubA_ephemeral(32B), nonceA(32B) }
B → A:  KEY_EXCH_REPLY  { pubB_ephemeral(32B), nonceB(32B) }

SharedSecret = X25519(privA_ephemeral, pubB_ephemeral)
            = X25519(privB_ephemeral, pubA_ephemeral)

LinkKey = HKDF-SHA256(
            IKM  = SharedSecret,
            salt = PSK,
            info = "link" || macA || macB,
            len  = 16
          )

A → B:  KEY_EXCH_CONFIRM { AES-GCM[LinkKey](challenge = nonceA XOR nonceB) }
B → A:  KEY_EXCH_CONFIRM { AES-GCM[LinkKey](challenge = nonceB XOR nonceA) }
```

**Properties:**
- PSK authentication: salt includes PSK; incorrect PSK → different LinkKey → challenge fails.
- Forward secrecy: ephemeral pairs destroyed after handshake.
- Anti-replay: nonceA and nonceB are 32B random values from the CSPRNG.

## Key rotation — Epoch + NACK + Renegotiation

- Default interval: 86,400s (24h). Configurable via `setKeyRotationInterval()`.
- **Sequence:**
  1. Receiver detects `epoch N+1` in a frame from a known peer while it expects `epoch N`.
  2. Sends `KEY_NACK { expected_epoch: N+1 }`.
  3. Buffer the rejected frame (max 1 per peer).
  4. Sender receives KEY_NACK → initiates KEY_EXCH_HELLO immediately.
  5. After successful KEY_EXCH_CONFIRM → retransmit buffered frame → purge buffer.
- **Battery nodes:** compare epoch in RTC memory with epoch of the first frame received on wake.

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

- **Storage:** Open-addressing hash table. Initial size: 16 slots.
- **LRU eviction:** If `heap_free < PEER_HEAP_LOW_WATERMARK` (20KB), evict the peer with oldest `lastSeen` and `routeCount == 0`.
- **Periodic cleanup:** Every 60s, remove peers with `lastSeen > PEER_TIMEOUT` (3600s).
- **Anti-replay:** Reject frame with `seq ≤ lastSeqRx` from the same peer. Sequence resets on each renegotiation.

## Key storage

- ESP32: NVS (native wear-leveling). Key: `"enigmang/peer/<mac_hex>"`.
- ESP8266: EEPROM (compact structure, fixed slot offset).

## Acceptance criteria

- Test: 2 nodes negotiate LinkKey. Third node with incorrect PSK cannot decrypt.
- Test: force epoch rotation. Verify KEY_NACK + renegotiation + retransmission within < 500ms.
- Test: frame with `seq ≤ lastSeqRx` is silently discarded.
- Test: LRU eviction when heap < 20KB.
