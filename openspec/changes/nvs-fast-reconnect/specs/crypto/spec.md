## MODIFIED Requirements

### Requirement: Key rotation — Epoch + NACK + Renegotiation
Epoch-based key rotation and immediate renegotiation on `KEY_NACK`. Applies directly to the fast-boot scenario: a node that wakes with a cached but expired link key will have its first DATA frame rejected with `KEY_NACK` by the peer (which does have a `PeerEntry` for it). The existing buffer-and-retransmit flow handles the recovery transparently.

Nodes with a wrong or absent PSK (not members of the network) have their KEY_EXCH frames silently discarded by the receiver (NetworkKey GCM authentication fails before any state is modified). No explicit rejection is sent to avoid amplification by misbehaving nodes.

A node that fast-boots and is **unknown to the peer** (peer lost its `PeerEntry`, e.g. due to its own reboot) will not receive a `KEY_NACK` — the DATA frame is silently dropped and the sender falls back to normal onboarding via the `FAST_BOOT_TIMEOUT_MS` watchdog.

#### Scenario: Fast-boot node with expired link key recovers via KEY_NACK
- **WHEN** a node fast-boots from NVS cache and sends a DATA frame with a cached (expired) link key
- **THEN** the peer SHALL send `KEY_NACK`, the node SHALL renegotiate the link key and retransmit the buffered frame within 500 ms of `KEY_EXCH_CONFIRM`

#### Scenario: Node with wrong PSK is silently ignored
- **WHEN** a node with an incorrect PSK sends `KEY_EXCH_HELLO`
- **THEN** the receiver SHALL silently discard the frame (NetworkKey GCM tag fails) without sending any response

#### Scenario: Fast-boot node unknown to peer falls back to onboarding
- **WHEN** a fast-booting node sends a DATA frame and the peer has no `PeerEntry` for it
- **THEN** the peer SHALL silently drop the frame and the sender SHALL fall back to normal onboarding after `FAST_BOOT_TIMEOUT_MS` elapses
