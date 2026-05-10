## Why

The `ProxyHandler` (ESP32 gateway) uses `PubSubClient` to connect to the MQTT broker on behalf of ESP8266 nodes. `PubSubClient` is an external Arduino dependency that is not available in native IDF environments (e.g., `gateway_hosted`) and lacks support for TLS, automatic reconnection, and MQTT v5. The `esp_mqtt_client` IDF component, already included in ESP-IDF 5.x, covers all these cases without additional dependencies.

## What Changes

- **Remove external dependency:** `knolleary/PubSubClient@^2.8` is removed from `platformio.ini` and from the source code.
- **Replace MQTT implementation:** `ProxyHandler` uses `esp_mqtt_client` (IDF) instead of `PubSubClient` + `WiFiClient`.
- **Internal API adaptation:** The constructor, `begin()`, `loop()`, and `_ensureMqttConnected()` of `ProxyHandler` are adapted to the event-based lifecycle of `esp_mqtt_client`.
- **No changes to proxy protocol:** The `PROXY_*` protocol between ESP8266 and ESP32 does not change. The `MeshNetwork` public API does not change.

## Capabilities

### New Capabilities

&lt;!-- No new user-visible capability is introduced. --&gt;
*(none)*

### Modified Capabilities

- `esp8266`: The gateway MQTT proxy now uses `esp_mqtt_client` internally. External behavior (PROXY_* protocol, public API) does not change, but the spec must reflect the IDF component as the reference implementation instead of PubSubClient.

## Impact

- **Affected files:** `src/ProxyHandler.h`, `src/ProxyHandler.cpp`, `platformio.ini`, `idf_component/idf_component.yml`.
- **ESP32 platform only:** The `#if !defined(ESP8266)` guard remains in effect; ESP8266 is not affected.
- **No breaking changes:** The public API (`setMqttBroker`, `setMqttBrokerAuth`) does not change.
- **Out of scope:** MQTT v5 support, TLS/SSL, migration of other MQTT clients (HTTPClient, PubSubClient used by end users via `getClient()`).