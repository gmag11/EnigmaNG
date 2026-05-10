# Spec: Onboarding and Channel Discovery

**Reference:** §5 of EnigmaNG Specs v2.md

## Purpose

Allow a new node to discover the mesh network, obtain the channel and configuration parameters, and join securely.

## Main mechanism: permanent AP on the gateway

The gateway runs a permanent SoftAP (ESP32 dual STA+AP mode simultaneously, with no significant performance penalty for expected ESP-NOW load).

**AP configuration:**

```
SSID:     ENIGMA-<NetworkID_HEX>-CH<CHANNEL>
Password: HMAC-SHA256(PSK, "onboarding")[:8] encoded in hex (16 chars)
```

> The password is derived from the PSK but does not expose it directly: brute-forcing the hash does not reveal the PSK in reasonable time.

**Provisioning protocol (HTTP over AP):**

```http
GET http://192.168.4.1/provision
→ 200 OK Content-Type: application/json
{
  "network_id": "A1B2",
  "channel": 6,
  "gateway_mac": "AA:BB:CC:DD:EE:FF",
  "ip_range": "10.200.0.0/16",
  "broker": "10.200.0.1:1883"
}
```

After receiving the response, the node:
1. Disconnects from the WiFi AP.
2. Configures ESP-NOW on the indicated channel.
3. Sends `JOIN_BEACON` encrypted with the NetworkKey.

## Fallback: blind channel scan

For preconfigured nodes that reboot or are out of gateway range:

1. WiFi scan for SSID `ENIGMA-*`. If found → use SSID channel.
2. If no SSID visible → scan channels 1 → 6 → 11 → rest, `CHANNEL_DWELL_TIME` = 200ms per channel.
3. Listen for `JOIN_BEACON` with Magic `0x454E` and NetworkID derived from the local PSK.
4. Gateways and relays transmit `JOIN_BEACON` every 5s in broadcast.

## JOIN_BEACON

Sent in broadcast with NetworkKey. Minimal content:

```
[NetworkID: 2B][Channel: 1B][GatewayMAC: 6B][HopCount: 1B][Flags: 1B]
```

Relays include the channel in their `ROUTE_ADV` (the `channel` field in flags), allowing nodes out of direct gateway range to learn the channel.

## Channel propagation

- Relays include the current channel in `ROUTE_ADV`.
- Priority: channel announced by the node with the lowest `hopCount` to the gateway.

## Acceptance criteria

- Test: new node with correct PSK connects via AP, receives provisioning JSON, configures channel and completes JOIN in < 5s.
- Test: node with incorrect PSK receives provisioning JSON but JOIN_BEACON fails (invalid GCM tag).
- Test: node that reboots (no AP visible) finds channel via blind scan in < 30s on channel 6.
- Test: node 2 hops from gateway discovers channel via relay's ROUTE_ADV.
