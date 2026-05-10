# Design: EnigmaNG v1.0

**Full reference:** EnigmaNG Specs v2.md (root file of the repository)

## Layered architecture

```
┌────────────────────────────────────────────────────────────┐
│  APPLICATION                                               │
│  PubSubClient / HTTPClient / any standard TCP library      │
│  Web UI + Prometheus (gateway-only)                        │
├────────────────────────────────────────────────────────────┤
│  TRANSPORT (lwIP)    TCP / UDP   MSS=176, WND=4×MSS       │
├────────────────────────────────────────────────────────────┤
│  IP LAYER (Layer 3)   netif "mesh0"   MTU=216   IPv4      │
│  DHCP server/client / distributed static table             │
├────────────────────────────────────────────────────────────┤
│  ROUTING (Layer 2.5)   DVR (RIPv2-like)                    │
│  RouteEntry×64=1.6KB   SeenFrameCache×32=384B              │
│  Split Horizon + Poison Reverse + ROUTE_WITHDRAW           │
├────────────────────────────────────────────────────────────┤
│  LINK (Layer 2)    22B header + 12B tag = 34B overhead     │
│  AES-128-GCM   Curve25519 ECDH   HKDF-SHA256               │
│  PeerEntry×16=576B (grows with open hash table)           │
├────────────────────────────────────────────────────────────┤
│  PHYSICAL (Layer 1)   QuickESPNow   RSSI EWMA α=0.3        │
└────────────────────────────────────────────────────────────┘
```

## Key design decisions

All decisions are justified in §§1–16 of the spec. Summary of rationale:

| Decision | Reference | Rationale |
|----------|-----------|-----------|
| AES-128-GCM (not 256) | §4.2 | 128 bits sufficient; key size halves PeerEntry memory cost |
| 12-byte tag (not 16) | §4.2 | NIST SP 800-38D minimum; saves 4B/frame |
| Derived, non-transmitted nonce | §4.1.3 | Saves 12B/frame; uniqueness via (Epoch, Seq, SrcMAC) |
| Seen-Frame Cache (no Path field) | §6.4 | Saves up to 49B/frame; fixed 384B RAM budget |
| Epoch + Reject (no overlap) | §4.3 | No double keys in RAM; ~100ms latency × 1/day tradeoff |
| Proactive DVR (not OLSR/BATMAN) | §6.1 | Simple, tested, scales up to ~100 nodes |
| Permanent AP (not transient) | §5.1 | ESP32 supports AP+STA; immediate onboarding |
| Full NAT (no LAN inbound routing) | §9.2 | No static routes on user's WiFi router; zero-config UX |
| HTTP Digest Auth (not Basic/JWT) | §11.2 | No plaintext password; no TLS/JWT in v1.0 |
| 1-byte Protocol field (EtherType-like) | §4.1.6 | L3 multiplexing; 256 values sufficient |

## RAM budget (ESP32)

| Component | RAM |
|-----------|-----|
| PeerManager (16 peers × 36B, grows dynamically) | ~576 B |
| RouteTable (64 entries × 25B) | ~1,600 B |
| Seen-Frame Cache (32 entries × 12B) | ~384 B |
| Battery downlink buffer (5 nodes × 5 msgs × 200B) | ~5,000 B |
| lwIP heap | ~20,000 B |
| Crypto (handshake, transient) | ~2,000 B |
| Protocol dispatcher (~8 handlers × 8B) | ~64 B |
| **Estimated total** | **~30 KB** |

ESP32 has 320KB DRAM. Sufficient margin for user application.

## Frame header (22 bytes)

```
Offset  Field       Size    Description
──────────────────────────────────────────────────────
0       Magic       2B      0x454E ("EN")
2       Version     1B      0x01
3       NetworkID   2B      HKDF(PSK, "netid", len=2)
5       FrameType   1B      enum FrameType (0x01–0x0F)
6       Protocol    1B      0x00=MESH_INTERNAL, 0x01=IPv4, ...
7       Epoch       1B      current key epoch (0–255)
8       SrcMAC      6B      end-to-end sender
14      DstMAC      6B      end-to-end receiver (FF×6=broadcast)
20      Sequence    2B      anti-replay + nonce component
22      [Payload]   ≤216B   AES-128-GCM encrypted
X       Tag         12B     GCM Auth Tag
```

## ECDH handshake (§4.2)

```
A ──KEY_EXCH_HELLO(pubA, nonceA)──────────────────────▶ B
A ◀─KEY_EXCH_REPLY(pubB, nonceB)───────────────────────  B

  SharedSecret = X25519(privA_eph, pubB_eph)
  LinkKey = HKDF(IKM=SharedSecret, salt=PSK, info="link"||macA||macB, len=16)

A ──KEY_EXCH_CONFIRM(GCM{nonceA XOR nonceB})──────────▶ B
A ◀─KEY_EXCH_CONFIRM(GCM{nonceB XOR nonceA})───────────  B
```

## Battery node cycle (§7)

```
[DEEP SLEEP] ──T_sleep──▶ [WAKE]
                              │
                          TX UPLINK ──▶ [Parent stores timestamp]
                              │
                          [RX1: 2s] ◀── downlink buffer flushed
                              │
                          [RX2: 2s] ◀── if pending
                              │
                          [DEEP SLEEP]
```

## Gateway: routing policy (§9.2)

```
dest in 10.200.0.0/16?  ──Yes──▶  RouteEntry table (mesh0, internal)
        │
        No
        ▼
   ip_napt masquerade  ──▶  wifi_sta (LAN or Internet)
   (src rewritten to gateway WiFi IP)
```

All outbound mesh traffic is NATed. No user router configuration required. The gateway itself has a direct WiFi IP (not NATed) for its Web UI and services.

## Source tree layout

See `public-api/spec.md` for the complete directory layout.

Modules are independent and can be compiled separately. The `Gateway` module is compiled only if `MESH_GATEWAY_ENABLED` is defined, saving flash on simple nodes.

## ESP32 + ESP8266 monorepo structure

**Decision:** `MeshNode8266` is included in this repository under the same `src/` directory, using preprocessor guards to separate platform code. Reasons:

1. Standard approach for multi-platform Arduino libraries (WiFiManager, AsyncMqttClient, etc.).
2. Compatible with PlatformIO and Arduino Library Manager expectations.
3. Protocol constants (22B header, `FrameType`, `Protocol`, `NetworkID`) live in a single shared file.

```
src/
  Protocol.h          ← shared constants ESP32+ESP8266 (no guards)
  MeshNetwork.h/cpp   ← #ifndef ESP8266 ... #endif
  Router.h/cpp        ← #ifndef ESP8266 ... #endif
  Crypto.h/cpp        ← #ifndef ESP8266 ... #endif
  NetifDriver.h/cpp   ← #ifndef ESP8266 ... #endif
  ... other ESP32 modules ...
  MeshNode8266.h      ← public API (no guards; safe to include on both)
  MeshNode8266.cpp    ← #ifdef ESP8266 ... #endif
examples/
  arduino/
    MeshNode8266/     ← ESP8266 example
```

Conventions for guards per file are described in the spec.

## External dependencies

| Library | Version | Purpose |
|---------|---------|---------|
| QuickESPNow | latest stable | Physical ESP-NOW layer |
| mbedTLS | IDF 5.5.4 built-in | Curve25519, HKDF, AES-GCM |
| lwIP | IDF 5.5.4 built-in | TCP/IP stack, netif, DHCP, mDNS |
| esp_http_server | IDF 5.5.4 built-in | Web UI + Digest Auth |
| esp_ping | IDF 5.5.4 built-in | Diagnostics (transparent) |
| Unity | PlatformIO built-in | Test framework |
