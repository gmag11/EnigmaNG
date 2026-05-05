# Spec: Capa Física (Physical Layer)

**Referencia:** §3 de EnigmaNG Specs v2.md

## Propósito

Abstracción limpia sobre QuickESPNow/ESP-NOW que el resto de la librería usa como capa de transporte. Provee envío unicast/broadcast, recepción con callback, RSSI por frame y gestión de canal.

## Interfaz requerida

```cpp
class MeshPhysicalLayer {
public:
    bool begin(uint8_t channel, const uint8_t* networkId);
    bool sendUnicast(const uint8_t* dstMac, const uint8_t* data, size_t len);
    bool sendBroadcast(const uint8_t* data, size_t len);
    void onReceive(MeshRecvCallback cb);
    int8_t getLastRssi();
    void setChannel(uint8_t channel);
    bool setTxPower(int8_t power);
};
```

## Gestión de canal

- Canal único para toda la red mesh (1–14).
- En modo single-chip gateway: canal forzado por el AP WiFi al que está conectado (restricción hardware).
- Cambio de canal: anunciado con `CONTROL/CHANNEL_CHANGE` + timestamp de migración (+30s). Los nodos conservan sus Link Keys.
- Nodos que pierden contacto tras el cambio inician búsqueda ciega (§5.2).

## RSSI y umbral de alcance

| Parámetro | Valor por defecto | Configurable |
|-----------|------------------|--------------|
| `RSSI_CONNECT_THRESHOLD` | -75 dBm | Sí |
| `RSSI_DISCONNECT_THRESHOLD` | -85 dBm | Sí |
| Hysteresis | 10 dB | No (derivada) |
| EWMA α | 0.3 | Sí |
| `PEER_INACTIVITY_TIMEOUT` | 120s | Sí |

### Fórmula EWMA

```
rssi_avg = 0.3 × rssi_nuevo + 0.7 × rssi_avg
```

Si no se recibe ningún frame en `PEER_INACTIVITY_TIMEOUT`, se invalida `rssi_avg`.

## Dependencias

- QuickESPNow (librería externa del autor).
- ESP-NOW API (IDF via Arduino Core ESP32 3.3.8).

## Criterio de aceptación

- Test: 2 ESP32 en canal 6. Enviar 100 frames unicast. Verificar entrega ≥ 95%, RSSI EWMA actualizado tras cada frame.
- Test: Cambio de canal anunciado. Ambos nodos migran y retoman comunicación sin renegociar clave.
