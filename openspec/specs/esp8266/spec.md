# Spec: Soporte ESP8266 — `src8266/MeshNode8266`

**Referencia:** §10 de EnigmaNG Specs v2.md

> **Estructura:** El código ESP8266 vive en el mismo directorio `src/` del repositorio, usando guards
> de preprocesador (`#ifdef ESP8266` / `#ifndef ESP8266`) para separar código por plataforma. Es el
> enfoque estándar de librerías Arduino multi-plataforma y es compatible con el registro de PlatformIO
> y Arduino Library Manager. Las constantes de protocolo compartidas están en `src/Protocol.h` sin guards.

## Propósito

Permitir que nodos ESP8266 participen en la red EnigmaNG como publicadores/suscriptores MQTT, a través de un ESP32 relay que actúa como proxy.

## Limitaciones aceptadas

- **Sin relay:** El ESP8266 no puede reenviar frames para otros nodos.
- **Sin stack IP propio:** No implementa `netif` virtual (requeriría parchar Arduino Core ESP8266).
- **Sin bridge:** No puede actuar como gateway.
- **Solo proxy MQTT:** Cubre el 95% de casos de uso IoT reales.

## Arquitectura: Proxy MQTT

```
[ESP8266] ──ESP-NOW──▶ [ESP32 Proxy] ──TCP/MQTT──▶ [Broker MQTT]
           PROXY_MSG                 MQTT publish/subscribe
```

El ESP32 proxy recibe frames `PROXY_MSG` del ESP8266 y los convierte a mensajes MQTT hacia el broker configurado en el gateway.

## Protocolo Proxy

```cpp
enum ProxyMsgType : uint8_t {
    PROXY_CONNECT      = 0x01,
    PROXY_PUBLISH      = 0x02,
    PROXY_SUBSCRIBE    = 0x03,
    PROXY_UNSUBSCRIBE  = 0x04,
    PROXY_MESSAGE      = 0x05,  // Broker → ESP8266
    PROXY_PUBACK       = 0x06,  // QoS1 confirmación
    PROXY_DISCONNECT   = 0x07,
    PROXY_DISCOVERY    = 0x08,  // Broadcast: buscar proxy disponible
    PROXY_OFFER        = 0x09   // Respuesta al discovery
};
```

**Selección de proxy:**
1. ESP8266 envía `PROXY_DISCOVERY` broadcast.
2. ESP32 vecinos responden `PROXY_OFFER` con su MAC y RSSI.
3. ESP8266 elige el de mejor RSSI y envía `PROXY_CONNECT`.

## Configuración del broker

- Dirección del broker: configurada en el gateway, distribuida en la respuesta de provisioning (§5.1) y en el `JOIN_BEACON`:
  ```json
  { "broker": "192.168.1.100:1883", "broker_user": "", "broker_pass": "" }
  ```
- ESP8266 almacena la dirección en EEPROM tras el primer provisioning.
- Actualiza si recibe un `JOIN_BEACON` con dirección diferente.

## API pública para ESP8266

```cpp
class MeshNode8266 {
public:
    bool begin(const char* psk);
    bool mqttPublish(const char* topic, const uint8_t* payload,
                     size_t len, uint8_t qos = 0, bool retain = false);
    bool mqttSubscribe(const char* topic, uint8_t qos = 0);
    bool mqttUnsubscribe(const char* topic);
    void onMqttMessage(MqttCallback cb);
    void onConnected(ConnectCallback cb);
    void setSleepDuration(uint32_t seconds);
    bool isConnected();
    void loop();
};
```

## Confirmación de entrega

- ESP-NOW confirma entrega peer-to-peer a nivel hardware (`OnDataSent`). Suficiente para el hop directo ESP8266↔ESP32 proxy.
- Para QoS 1/2: el broker proporciona PUBACK. El proxy lo reenvía al ESP8266 via `PROXY_PUBACK`.

## Criterio de aceptación

- Test: ESP8266 hace `PROXY_DISCOVERY`, selecciona proxy ESP32, publica en topic. Verificar mensaje llega al broker MQTT.
- Test: broker publica en topic suscrito por ESP8266. Verificar que `onMqttMessage` es llamado en el ESP8266.
- Test: ESP8266 con PSK incorrecta no puede conectar (JOIN_BEACON tag inválido).
- Test: cambio de broker en gateway. ESP8266 recibe nueva dirección en siguiente JOIN_BEACON y la aplica.
