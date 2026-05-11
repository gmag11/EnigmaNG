## 1. Protocol — Frame types

- [ ] 1.1 Add `TIME_REQ = 0x10` and `TIME_SYNC = 0x11` to `FrameType` enum in `src/LinkLayer.h`
- [ ] 1.2 Define `TimeSyncReq` (4 bytes) and `TimeSyncResp` (12 bytes) structs in `src/LinkLayer.h`

## 2. Public API — MeshNetwork

- [ ] 2.1 Add `syncTime()`, `isTimeSynced()`, and `enableNTP()` declarations to `src/MeshNetwork.h`
- [ ] 2.2 Update `MeshTimeCallback` typedef to `void (*)(time_t, bool fromNTP)` in `src/MeshNetwork.h`
- [ ] 2.3 Add private members `_timeSynced`, `_ntpServer1/2`, `_ntpTz`, `_lastTimeBroadcastMs` to `MeshNetwork`

## 3. Gateway — SNTP initialisation

- [ ] 3.1 Implement `enableNTP()` in `src/MeshNetwork.cpp` — store server names and TZ string
- [ ] 3.2 In the `ARDUINO_EVENT_WIFI_STA_GOT_IP` handler, call `_initSNTP()` which calls `esp_sntp_setoperatingmode`, `esp_sntp_setservername`, `esp_sntp_init`, and `sntp_set_time_sync_notification_cb`
- [ ] 3.3 Implement SNTP callback `_onNtpSync()`: set `_timeSynced = true`, fire `_onTimeSyncCb(time(nullptr), true)`, call `_broadcastTimeSync()`
- [ ] 3.4 Apply timezone in `_initSNTP()` via `setenv("TZ", _ntpTz, 1)` + `tzset()`

## 4. Gateway — TIME_REQ handler and broadcast

- [ ] 4.1 Add `TIME_REQ` case to the frame dispatch in `MeshNetwork::_handleFrame()`: call `_sendTimeSyncResponse(srcMac, req.tx_ms)`
- [ ] 4.2 Implement `_sendTimeSyncResponse(dstMac, req_tx_ms)`: build `TimeSyncResp`, fill `unix_sec/ms` from `gettimeofday()`, echo `req_tx_ms`, measure `gw_proc_ms`, send via `_sendFrame`
- [ ] 4.3 Implement `_broadcastTimeSync()`: iterate established peers, call `_sendTimeSyncResponse(peerMac, 0)` for each
- [ ] 4.4 In `MeshNetwork::loop()`, add periodic call to `_broadcastTimeSync()` every `MESH_TIME_BROADCAST_INTERVAL_MS` (default 600 000 ms) when `_timeSynced && isGateway()`
- [ ] 4.5 In the handshake-complete path (`_onPeerHandshakeComplete`), if `isGateway() && _timeSynced`, call `_sendTimeSyncResponse(peerMac, 0)`

## 5. Node — TIME_SYNC handler and syncTime()

- [ ] 5.1 Add `TIME_SYNC` case to frame dispatch in `_handleFrame()`: call `_applyTimeSync(resp)`
- [ ] 5.2 Implement `_applyTimeSync(resp)`: compute RTT correction if `req_tx_ms != 0`, call `settimeofday()` or `adjtime()`, set `_timeSynced = true`, fire callback
- [ ] 5.3 Implement `syncTime()`: find gateway peer with completed session, send `TIME_REQ` with current `millis()`, return `true/false`
- [ ] 5.4 Implement `isTimeSynced()`: return `_timeSynced`

## 6. Battery node — RTC clock persistence

- [ ] 6.1 In `BatteryNode::begin()` (wake path), before first frame: if `_state->lastMeshTime > 1000000000`, call `settimeofday()` and set `_timeSynced = true`
- [ ] 6.2 In `_applyTimeSync()`, call `BatteryNode::setMeshTime(corrected_t)` when running in battery mode (`_mode == MESH_BATTERY`)
- [ ] 6.3 Add stale-clock check in battery node active window: if elapsed since `lastMeshTime` > `MESH_TIME_STALE_THRESHOLD_S` (default 3600), call `syncTime()`

## 7. Testing and validation

- [ ] 7.1 Add unit test in `test/test_link_layer/` verifying `TimeSyncResp` serialisation round-trip
- [ ] 7.2 Manual integration test: flash gateway + NodePing; verify `time(nullptr)` returns plausible epoch on the node after join
- [ ] 7.3 Verify NAPT still works after adding SNTP init (no regression in WiFi uplink handler)
- [ ] 7.4 Test battery node: verify `lastMeshTime` is restored after deep-sleep cycle and `time(nullptr)` returns correct value on wake before first network exchange
