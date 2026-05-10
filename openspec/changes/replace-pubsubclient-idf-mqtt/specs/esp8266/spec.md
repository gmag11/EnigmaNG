## MODIFIED Requirements

### Requirement: MQTT proxy client on ESP32 gateway
The ESP32 gateway SHALL use `esp_mqtt_client` (native IDF component) as the MQTT client to connect to the broker on behalf of ESP8266 nodes. The use of `PubSubClient` (external Arduino library) as the reference implementation is not permitted.

The MQTT connection SHALL be maintained automatically via the `esp_mqtt_client` built-in reconnection mechanism; no manual reconnection logic shall be required in `ProxyHandler::loop()`.

Upon receiving the `MQTT_EVENT_CONNECTED` event, the proxy SHALL resubscribe all topics of active `ProxyClient` clients.

Upon receiving the `MQTT_EVENT_DATA` event, the proxy SHALL forward the message to the corresponding ESP8266 subscriber via a `PROXY_MESSAGE` frame.

#### Scenario: Initial broker connection
- **WHEN** `ProxyHandler::begin()` is invoked with a configured broker host and port
- **THEN** the system SHALL call `esp_mqtt_client_init()` and `esp_mqtt_client_start()` to initiate the asynchronous MQTT connection

#### Scenario: Resubscription after reconnection
- **WHEN** the MQTT client emits the `MQTT_EVENT_CONNECTED` event after a reconnection
- **THEN** the proxy SHALL resubscribe all active topics of all connected `ProxyClient`s

#### Scenario: Forwarding message from broker to ESP8266
- **WHEN** the broker publishes a message to a topic subscribed by a proxied ESP8266
- **THEN** the proxy SHALL construct and send a `PROXY_MESSAGE` frame to the ESP8266 subscriber within the same MQTT event callback invocation

#### Scenario: No PubSubClient dependency
- **WHEN** the project is compiled for ESP32
- **THEN** compilation SHALL complete without requiring the `knolleary/PubSubClient` dependency