# Spec Delta: Gateway WiFi
# Change: mesh-time-sync

## ADDED Requirements

### Requirement: Gateway initialises SNTP on WiFi uplink connect
When the gateway receives the `ARDUINO_EVENT_WIFI_STA_GOT_IP` event, it SHALL initialise IDF SNTP (`esp_sntp_init()`) with the configured servers and register a sync callback. SNTP SHALL use poll mode (`SNTP_OPMODE_POLL`). The timezone SHALL be applied via `setenv("TZ", tz, 1)` + `tzset()`.

#### Scenario: Uplink connects
- **WHEN** the gateway's WiFi STA interface receives an IP address
- **THEN** `esp_sntp_init()` is called and SNTP polling begins

#### Scenario: SNTP sync achieved
- **WHEN** the SNTP callback fires with a valid time (epoch > 1 000 000 000)
- **THEN** `isTimeSynced()` returns `true` and a `TIME_SYNC` is sent to all established peers

### Requirement: Gateway responds to TIME_REQ
On receiving a `TIME_REQ` frame from a peer with a completed session, the gateway SHALL send a unicast `TIME_SYNC` response if and only if `isTimeSynced()` is `true`.

#### Scenario: Synced gateway receives TIME_REQ
- **WHEN** a `TIME_REQ` arrives from an established peer and the gateway clock is synced
- **THEN** a `TIME_SYNC` response is sent within 50 ms with correct `unix_sec`, `unix_ms`, echoed `req_tx_ms`, and measured `gw_proc_ms`

#### Scenario: Unsynced gateway receives TIME_REQ
- **WHEN** a `TIME_REQ` arrives but SNTP has not yet completed
- **THEN** no response is sent
