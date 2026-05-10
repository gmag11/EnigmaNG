# Spec: Battery Nodes

**Reference:** §7 of EnigmaNG Specs v2.md

## Purpose

Support for battery-powered ESP32 nodes that minimize consumption through cyclic deep sleep. Model inspired by LoRaWAN Class A.

## Lifecycle (Class A)

```
[DEEP SLEEP] ──(T_sleep)──▶ [WAKE] ──▶ [TX UPLINK] ──▶ [RX1: 2s] ──▶ [RX2: 2s] ──▶ [SLEEP]
```

- The node only listens immediately after transmitting (2 RX windows).
- No active polling. Radio is turned off outside the windows.
- Default `BATTERY_HEARTBEAT_INTERVAL`: 3600s (1h). Configurable.

## Parent Node

- The battery node selects its Parent during join: the relay neighbor with best `avgRssi`.
- The Parent keeps a per-child FIFO downlink buffer:
  - Maximum 5 messages per child.
  - Maximum 200 bytes per message.
  - On receiving the UPLINK, it flushes the buffer during RX1 + RX2 windows.
- If no heartbeat arrives within `3 × HEARTBEAT_INTERVAL`: it frees the buffer and removes the child's routes.

## Clock synchronization

The Parent includes a timestamp (approximate UTC seconds) in the UPLINK response. The battery node stores the offset in `RTC_DATA_ATTR` (persists across deep sleep).

**Uses:**
- Precise wake-ups with `esp_sleep_enable_timer_wakeup()`.
- Checking key epoch expiration upon wake.
- Timestamps in telemetry.

## Parent rediscovery (3 candidates stored in NVS)

The battery node stores up to **3 Parent candidates** in NVS (MAC + last `avgRssi`), updated on each successful UPLINK.

**On wake:**
1. Try UPLINK to primary Parent (4s timeout: RX1 + RX2).
2. If no ACK → try the second candidate (4s).
3. If no ACK → try the third (4s).
4. If all three fail → blind channel scan (§5.2) + full re-join.

**NVS cost:** ~20 bytes for 3 entries `(mac[6], rssi[1]) × 3`.

## Epoch management in battery nodes

On waking from deep sleep, the node compares the epoch stored in `RTC_DATA_ATTR` with the epoch of the first frame received from the Parent. If they differ: start key renegotiation before sending data.

## Configurable modes

| Parameter | Default | API |
|-----------|---------|-----|
| Sleep interval | 3600s | `setBatteryMode(true, seconds)` |
| Heartbeat interval | 3600s | `BATTERY_HEARTBEAT_INTERVAL` |
| RX1 window | 2s | Internal constant |
| RX2 window | 2s | Internal constant |
| Max downlink buffer per child | 5 msgs | `BATTERY_DOWNLINK_BUFFER_SIZE` |

## Acceptance criteria

- Test: battery node cycles every 60s. It sends an UPLINK and receives a response in RX1 or RX2 window.
- Test: 5 downlink messages in buffer. Node receives them all in the windows after the UPLINK.
- Test: primary Parent disappears. Node recovers connectivity via secondary candidate without re-joining.
- Test: epoch changes during deep sleep. Node detects the change on wake and renegotiates before sending data.
- Test: battery node DOES NOT act as relay (does not forward frames from other nodes).
