## Context

The `ProxyHandler` acts as an MQTT bridge on the ESP32 gateway: it receives `PROXY_*` frames from ESP8266 nodes via ESP-NOW and translates them into MQTT publications/subscriptions to the broker. The current implementation uses `PubSubClient` (external Arduino library) over a blocking `WiFiClient`. This decision was pragmatic for v1.0 but introduces an unnecessary external dependency in an environment where `esp_mqtt_client` is already available in the SDK.

Current state:
- `ProxyHandler` contains a `WiFiClient _tcpClient` and a `PubSubClient _mqtt` as members.
- The connection is synchronous (blocking). Reconnection is managed with a manual 5 s backoff.
- `loop()` calls `_mqtt.loop()` on every cycle to receive incoming messages.
- `_mqttCallback` is a static global function pointer.
- Shared subscription (a single MQTT client on the broker for all ESP8266s) assigns all topics to the same callback.

## Goals / Non-Goals

**Goals:**
- Replace `PubSubClient` + `WiFiClient` with `esp_mqtt_client` (native IDF).
- Eliminate the `knolleary/PubSubClient` dependency from `platformio.ini`.
- Keep the `PROXY_*` protocol and the `MeshNetwork` public API identical.
- Manage reconnections automatically via the IDF event system.
- Compatibility with `ESP_IDF &gt;= 5.x` (used in Arduino Core ESP32 v3.3.x).

**Non-Goals:**
- TLS/SSL support for the broker connection.
- MQTT v5.
- Multiple MQTT connections (one per ESP8266 instead of one shared).
- Changes to the PROXY_* protocol or the `MeshNetwork` public API.

## Decisions

### D1: Single shared `esp_mqtt_client` instance

**Decision:** Maintain a single MQTT connection to the broker, shared by all proxied ESP8266s, just like with PubSubClient.

**Discarded alternatives:**
- _One connection per ESP8266:_ Would require multiple `esp_mqtt_client` instances, increase memory usage (each instance ≈ 2–4 KB of heap), and demand multiple simultaneous TCP sockets.

**Rationale:** The shared subscription architecture with a single gateway client-ID is the simplest and most efficient for this use case.

### D2: Event-driven management instead of polling

**Decision:** `esp_mqtt_client` uses an event-based model (`esp_mqtt_event_id_t`). The event callback replaces the static global callback of PubSubClient.

```
MQTT_EVENT_CONNECTED    → resume saved subscriptions
MQTT_EVENT_DISCONNECTED → log; reconnection managed automatically by IDF
MQTT_EVENT_DATA         → forward message to corresponding ESP8266 (PROXY_MESSAGE)
MQTT_EVENT_ERROR        → log TLS/TCP error
```

**Rationale:** Eliminates the blocking polling of `_mqtt.loop()`. The `ProxyHandler::loop()` no longer needs to call any MQTT library function; it only manages its own proxy client state.

### D3: Maintenance of `esp_mqtt_client_handle_t` member

**Decision:** `ProxyHandler` stores the handle `_mqttHandle` as a private member (`esp_mqtt_client_handle_t _mqttHandle = nullptr`).

**Lifecycle:**
```
begin() → esp_mqtt_client_init()  → esp_mqtt_client_start()
~ProxyHandler() → esp_mqtt_client_stop() → esp_mqtt_client_destroy()
```

### D4: Resubscription on MQTT_EVENT_CONNECTED

**Decision:** Upon receiving `MQTT_EVENT_CONNECTED`, the handler iterates over all active `ProxyClient`s and resubscribes their topics, just like the current implementation with PubSubClient.

### D5: PROXY_MESSAGE dispatch to the correct ESP8266

**Problem:** With PubSubClient, the callback receives topic+payload and must determine which ESP8266 to forward to. The current logic searches which client has that topic subscribed. With `esp_mqtt_client`, the mechanism is the same but the callback is a function with `void* user_context`.

**Decision:** Pass `this` as `user_context` in `esp_mqtt_client_config_t`. The static callback casts it to `ProxyHandler*` and delegates to `_onMqttData(event)`.

## Risks / Trade-offs

- **[Risk] IDF API not available on ESP8266:** `esp_mqtt_client` does not exist in Arduino Core ESP8266. Mitigation: the `#if !defined(ESP8266)` guard already exists in all affected files; no changes required here.
- **[Risk] `esp_mqtt_client` requires FreeRTOS task context:** The event callback may execute from a different IDF task. Mitigation: `esp_mqtt_client` in IDF ≥ 5.x executes the callback in the client's internal task. The `ProxyHandler` fields accessed from the callback must be thread-safe. For simplicity (same as today with PubSubClient), single-threaded execution from the Arduino `loop()` is assumed; a note is added in code.
- **[Trade-off] `ProxyHandler::loop()` becomes almost empty:** With the event model, `loop()` no longer does anything related to MQTT. It is kept for compatibility in case future timeout logic is needed.

## Migration Plan

1. Remove `#include &lt;PubSubClient.h&gt;` and `#include &lt;WiFiClient.h&gt;` from `ProxyHandler.h`.
2. Add `#include &lt;mqtt_client.h&gt;` (IDF).
3. Replace members `_tcpClient` and `_mqtt` with `esp_mqtt_client_handle_t _mqttHandle`.
4. Adapt `begin()`: `esp_mqtt_client_init()` + `esp_mqtt_client_start()`.
5. Adapt `loop()`: remove call to `_mqtt.loop()`.
6. Adapt `_ensureMqttConnected()`: no longer needed; IDF manages reconnection.
7. Implement static `_mqttEventHandler()` that replaces `_mqttCallback`.
8. Adapt publish/subscribe/unsubscribe to `esp_mqtt_client_publish()` / `esp_mqtt_client_subscribe()` / `esp_mqtt_client_unsubscribe()`.
9. Remove `knolleary/PubSubClient@^2.8` from all sections of `platformio.ini`.
10. Add `idf_component("esp_mqtt")` if not already a transitive dependency in `idf_component.yml`.

**Rollback:** The previous branch preserves the PubSubClient implementation. No protocol changes nor on-disk data format changes.

## Open Questions

- Should TLS support be added in this iteration, now that `esp_mqtt_client` supports it natively? (Proposal: no, out of scope for this change, open separate issue.)
- Is the `esp_mqtt` component a transitive dependency of `esp_wifi` or must it be declared explicitly in `idf_component.yml`? (Verify during implementation.)
