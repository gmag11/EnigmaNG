## 1. Preparation and dependencies

- [ ] 1.1 Remove `knolleary/PubSubClient@^2.8` from all sections of `platformio.ini` (environments `esp32`, `test_*`, etc.). _Test: `pio pkg list` does not show PubSubClient._
- [ ] 1.2 Verify that `esp_mqtt` is a transitive dependency of `esp_wifi` in IDF 5.5.x or add it explicitly in `idf_component/idf_component.yml`. _Test: native IDF compilation completes without `esp_mqtt_client_init` symbol error._

## 2. Refactoring ProxyHandler.h

- [ ] 2.1 Remove `#include &lt;PubSubClient.h&gt;` and `#include &lt;WiFiClient.h&gt;` from the header. _Test: header compiles without including PubSubClient._
- [ ] 2.2 Add `#include &lt;mqtt_client.h&gt;` inside the `#if !defined(ESP8266)` guard. _Test: compilation with no unknown symbol warning._
- [ ] 2.3 Replace members `WiFiClient _tcpClient` and `PubSubClient _mqtt` with `esp_mqtt_client_handle_t _mqttHandle = nullptr`. _Test: `sizeof(ProxyHandler)` compiles and contains no reference to PubSubClient._
- [ ] 2.4 Remove the declaration of `_ensureMqttConnected()` and the static function pointer `_mqttCallback`. Add declaration of `_mqttEventHandler(void*, esp_event_base_t, int32_t, void*)`. _Test: header with no PubSubClient references._

## 3. Refactoring ProxyHandler.cpp — Initialization

- [ ] 3.1 Rewrite the constructor: remove `: _mqtt(_tcpClient)` and `s_proxyInstance` initialization. _Test: clean compilation._
- [ ] 3.2 Rewrite `begin()`: construct `esp_mqtt_client_config_t` with host, port, client_id, and credentials; call `esp_mqtt_client_init()` + `esp_mqtt_client_register_event()` + `esp_mqtt_client_start()`. _Test: `begin()` compiles and does not throw a linkage error._
- [ ] 3.3 Remove `_ensureMqttConnected()` and its call in `loop()`. Simplify `loop()` so it performs no MQTT operations. _Test: `loop()` does not call any PubSubClient symbol._
- [ ] 3.4 Add destructor `~ProxyHandler()` that calls `esp_mqtt_client_stop()` and `esp_mqtt_client_destroy()` if `_mqttHandle != nullptr`. _Test: no handle leak when destroying `ProxyHandler`._

## 4. Refactoring ProxyHandler.cpp — Event handler

- [ ] 4.1 Implement the static handler `_mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data)` with `static_cast&lt;ProxyHandler*&gt;(handler_args)` to obtain `this`. _Test: callback registers with no type warning._
- [ ] 4.2 In `MQTT_EVENT_CONNECTED`: resubscribe all active topics of `ProxyClient`s using `esp_mqtt_client_subscribe()`. _Test: after simulated reconnection, all topics are resubscribed._
- [ ] 4.3 In `MQTT_EVENT_DATA`: extract topic and payload from `esp_mqtt_event_handle_t`; search for the subscriber client; call `_sendProxyMessage()`. _Test: message from broker reaches the correct ESP8266 as `PROXY_MESSAGE`._
- [ ] 4.4 In `MQTT_EVENT_ERROR`: print error log (type, TCP/TLS code). _Test: log visible in Serial with error information._

## 5. Refactoring MQTT operations

- [ ] 5.1 Replace `_mqtt.publish(topic, payload, len, retain)` in `_handlePublish()` with `esp_mqtt_client_publish(_mqttHandle, topic, (const char*)payload, len, qos, retain)`. _Test: ESP8266 publication reaches the broker._
- [ ] 5.2 Replace `_mqtt.subscribe(topic)` in `_handleSubscribe()` and in resubscription with `esp_mqtt_client_subscribe(_mqttHandle, topic, 0)`. _Test: subscription registered in broker; broker delivers messages to the topic._
- [ ] 5.3 Replace `_mqtt.unsubscribe(topic)` in `_handleUnsubscribe()` and `_handleDisconnect()` with `esp_mqtt_client_unsubscribe(_mqttHandle, topic)`. _Test: after unsubscribe, broker no longer delivers messages to the topic._
- [ ] 5.4 Replace the `_mqtt.connected()` check with internal state verification (boolean flag `_mqttConnected` updated in `MQTT_EVENT_CONNECTED` / `MQTT_EVENT_DISCONNECTED`). _Test: publish returns error if MQTT is not connected._

## 6. Compilation verification

- [ ] 6.1 Compile `esp32` environment with Arduino Core: `pio run -e esp32`. _Test: no errors or warnings related to PubSubClient._
- [ ] 6.2 Compile native IDF environment `gateway_hosted`: `idf.py build`. _Test: no MQTT symbol errors._
- [ ] 6.3 Compile `esp8266` environment: `pio run -e esp8266`. _Test: compilation unchanged (guard `#if !defined(ESP8266)` protects modified code)._

## 7. Integration tests

- [ ] 7.1 End-to-end test: ESP8266 does `PROXY_DISCOVERY` → `PROXY_CONNECT` → `PROXY_PUBLISH`. Verify the message reaches the MQTT broker. _Test: message visible in external MQTT client subscribed to the topic._
- [ ] 7.2 Reconnection test: cut TCP connection to broker and restore it. Verify the proxy reconnects and resubscribes without manual intervention. _Test: after reconnection, broker messages reach the ESP8266 again._
- [ ] 7.3 Multiple clients test: connect 3 different ESP8266s with different topics. Verify broker messages are routed to the correct ESP8266. _Test: each ESP8266 receives only its messages.
